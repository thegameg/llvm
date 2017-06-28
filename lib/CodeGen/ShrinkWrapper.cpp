//===- lib/CodeGen/ShrinkWrapper.cpp - Shrink Wrapping Utility --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Shrink-wrapper implementation.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

#include "llvm/CodeGen/ShrinkWrapper.h"

// FIXME: ShrinkWrap2: Name
#define DEBUG_TYPE "shrink-wrap2"

#define VERBOSE_DEBUG(X)                                                       \
  do {                                                                         \
    if (VerboseDebug)                                                          \
      DEBUG(X);                                                                \
  } while (0);

using namespace llvm;

static cl::opt<cl::boolOrDefault>
    VerboseDebug("shrink-wrap-verbose", cl::Hidden,
                 cl::desc("verbose debug output"));

void ShrinkWrapInfo::determineCSRUses() {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Walk all the uses of each callee-saved register, and map them to their
  // basic blocks.
  const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();

  BitVector CSRegUnits(TRI.getNumRegUnits());
  DenseMap<unsigned, unsigned> RegUnitToCSRIdx;
  for (unsigned i = 0; CSRegs[i]; ++i) {
    for (MCRegUnitIterator RegUnit(CSRegs[i], &TRI); RegUnit.isValid();
         ++RegUnit) {
      RegUnitToCSRIdx[*RegUnit] = i;
      CSRegUnits.set(*RegUnit);
    }
  }

  auto MarkAsUsedBase = [&](unsigned RegIdx, unsigned MBBNum) {

    BitVector &Used = Uses[MBBNum];
    if (Used.empty())
      Used.resize(getNumResultBits());
    Used.set(RegIdx);
  };
  auto MarkAsUsed = [&](unsigned RegIdx, const MachineBasicBlock &MBB,
                        bool isTerminator = false) {
    unsigned MBBNum = MBB.getNumber();
    MarkAsUsedBase(RegIdx, MBBNum);
    MarkAsUsedBase(RegIdx ^ 1, MBBNum);
    // If it's a terminator, mark the successors as used as well,
    // since we can't save after a terminator (i.e. cbz w23, #10).
    if (isTerminator)
      for (MachineBasicBlock *Succ : MBB.successors()) {
        MarkAsUsedBase(RegIdx, Succ->getNumber());
        MarkAsUsedBase(RegIdx ^ 1, Succ->getNumber());
      }
  };

  // FIXME: ShrinkWrap2: Naked functions.
  // FIXME: ShrinkWrap2: __builtin_unwind_init.

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {

        if (MO.isRegMask()) {
          // Check for regmasks only on the original CSR, as the aliases are not
          // always there.
          for (unsigned i = 0; CSRegs[i]; ++i)
            if (MO.clobbersPhysReg(CSRegs[i]))
              MarkAsUsed(i, MBB, MI.isTerminator());
        } else if (MO.isReg() && MO.getReg() && (MO.readsReg() || MO.isDef())) {
          for (MCRegUnitIterator RegUnit(MO.getReg(), &TRI); RegUnit.isValid();
               ++RegUnit)
            if (CSRegUnits.test(*RegUnit))
              MarkAsUsed(RegUnitToCSRIdx[*RegUnit], MBB, MI.isTerminator());
        }
      }
    }
  }
}

const BitVector *ShrinkWrapInfo::getUses(unsigned MBBNum) const {
  auto& Use = Uses[MBBNum];
  if (Use.empty())
    return nullptr;
  return &Use;
}

ShrinkWrapper::SCCLoopInfo::SCCLoopInfo(const MachineFunction &MF) {
  // Create the SCCLoops.
  for (auto I = scc_begin(&MF); !I.isAtEnd(); ++I) {
    // Skip non-loop SCCs.
    if (!I.hasLoop())
      continue;

    SCCs.emplace_back();
    // The SCCLoop number is the first basic block number in the SCC.
    unsigned Number = (*I->begin())->getNumber();
    SCCs.back().Number = Number;
    SCCs.back().Size = I->size();

    // The number used in MBBToSCC is the position of the SCC in `SCCs`
    for (const MachineBasicBlock *MBB : *I)
      MBBToSCC[MBB->getNumber()] = SCCs.size() - 1;
  }

  // Compute successors / predecessors of the SCCLoops.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineBasicBlock *Succ : MBB.successors()) {
      SCCLoop *MBBSCC = getSCCLoopFor(MBB.getNumber());
      SCCLoop *SuccSCC = getSCCLoopFor(Succ->getNumber());
      // The successor is a loop, but not the current block. It means the
      // successor's predecessor is the current block.
      if (!MBBSCC && SuccSCC)
        SuccSCC->Predecessors.insert(&MBB);
      // The successor is not a loop, but the current block is one. It means
      // that the loop's successor is the block's successor.
      else if (MBBSCC && !SuccSCC)
        MBBSCC->Successors.insert(Succ);
      // The successor and the block are loops. We now need to connect SCCs
      // together.
      else if (MBBSCC && SuccSCC && MBBSCC != SuccSCC) {
        MBBSCC->Successors.insert(Succ);
        SuccSCC->Predecessors.insert(&MBB);
      }
    }
    for (const MachineBasicBlock *Pred : MBB.predecessors()) {
      SCCLoop *MBBSCC = getSCCLoopFor(MBB.getNumber());
      SCCLoop *PredSCC = getSCCLoopFor(Pred->getNumber());
      // The predecessor is a loop, but not the current block. It means the
      // predecessor's successor is the current block.
      if (!MBBSCC && PredSCC)
        PredSCC->Successors.insert(&MBB);
      // The predecessor is not a loop, but the current block is one. It
      // means that the loop's predecessor is the block's predecessor.
      else if (MBBSCC && !PredSCC)
        MBBSCC->Predecessors.insert(Pred);
      // The successor and the block are loops. We now need to connect SCCs
      // together.
      else if (MBBSCC && PredSCC && MBBSCC != PredSCC) {
        MBBSCC->Predecessors.insert(Pred);
        PredSCC->Successors.insert(&MBB);
      }
    }
  }
}

unsigned ShrinkWrapper::blockNumber(unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return C->getNumber();
  return MBBNum;
}

iterator_range<MBBIterator>
ShrinkWrapper::blockSuccessors(unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return {C->Successors.begin(), C->Successors.end()};
  const MachineBasicBlock *MBB = MF.getBlockNumbered(MBBNum);
  return {&*MBB->succ_begin(), &*MBB->succ_end()};
}

iterator_range<MBBIterator>
ShrinkWrapper::blockPredecessors(unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return {C->Predecessors.begin(), C->Predecessors.end()};
  const MachineBasicBlock *MBB = MF.getBlockNumbered(MBBNum);
  return {&*MBB->pred_begin(), &*MBB->pred_end()};
}

void ShrinkWrapper::determineUses() {
  // FIXME: ShrinkWrap2: We do unnecessary copies here.
  for (const MachineBasicBlock &MBB : MF) {
    if (const TargetResultSet *Use = SWI->getUses(MBB.getNumber())) {
      unsigned MBBNum = blockNumber(MBB.getNumber());
      Uses[MBBNum].resize(SWI->getNumResultBits());
      Uses[MBBNum] |= *Use;
    }
  }
}

void ShrinkWrapper::removeUsesOnNoReturnPaths() {
  NoReturnBlocks.resize(MF.getNumBlockIDs());

  // Mark all reachable blocks from any return blocks.
  for (const MachineBasicBlock &MBB : MF)
    if (MBB.isReturnBlock())
      for (const MachineBasicBlock *Block : inverse_depth_first(&MBB))
        NoReturnBlocks.set(Block->getNumber());

  // Flip, so that we can get the non-reachable blocks.
  NoReturnBlocks.flip();

  for (unsigned MBBNum : NoReturnBlocks.set_bits()) {
    DEBUG(dbgs() << "Remove uses from no-return BB#" << MBBNum << '\n');
    Uses[MBBNum].clear();
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ShrinkWrapper::dumpUses() const {
  for (const auto& Use : enumerate(Uses)) {
    if (!Use.value().count())
      continue;

    dbgs() << "BB#" << Use.index() << " uses : ";
    int Elt = Use.value().find_first();
    if (Elt >= 0)
      SWI->printElt(Elt, dbgs());
    for (Elt = Use.value().find_next(Elt); Elt > 0;
         Elt = Use.value().find_next(Elt)) {
      dbgs() << ", ";
      SWI->printElt(Elt, dbgs());
    }
    dbgs() << '\n';
  }
}
#endif // LLVM_ENABLE_DUMP

void ShrinkWrapper::markUsesOutsideLoops() {
  // Keep track of the elements to attach to a basic block.
  SparseBBResultSetMap ToInsert;
  for (const auto &Use : enumerate(Uses)) {
    unsigned MBBNum = Use.index();
    const TargetResultSet &Elts = Use.value();

    auto Mark = [&](const MachineBasicBlock *Block) {
      unsigned BlockNum = Block->getNumber();
      TargetResultSet &ToInsertTo = ToInsert[BlockNum];
      if (ToInsertTo.empty())
        ToInsertTo.resize(SWI->getNumResultBits());
      ToInsertTo |= Elts;
      VERBOSE_DEBUG(dbgs() << "Mark: BB#" << BlockNum << '\n');
    };

    if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum)) {
      DEBUG(dbgs() << "Loop for CSR: BB#" << MBBNum << '\n');

      // Mark all the entry blocks of the loop.
      for (const MachineBasicBlock *Block : C->predecessors())
        Mark(Block);

      // Mark all the exit blocks of the loop.
      for (const MachineBasicBlock *Exit : C->successors())
        Mark(Exit);
    }
  }

  for (auto &KV : ToInsert)
    Uses[blockNumber(KV.first)] |= KV.second;
}

void ShrinkWrapper::computeAttributes(
    unsigned Elt, AttributeMap &Attrs,
    ReversePostOrderTraversal<const MachineFunction *> &RPOT) const {
  auto UsesElt = [&](unsigned MBBNum) {
    auto &Use = Uses[MBBNum];
    if (Use.empty())
      return false;
    return Use.test(Elt);
  };

  auto Assign = [&](TargetResultSet &Set, bool New) {
    if (Set.test(Elt) != New)
      Set.flip(Elt);
  };

  // Count how many times we visited a SCCLoop.
  DenseMap<const SCCLoop *, unsigned> SCCVisited;

  // PO traversal for anticipation computation. We want to handle the SCC only
  // when we reach the *LAST* component.
  for (const MachineBasicBlock *MBB : make_range(RPOT.rbegin(), RPOT.rend())) {
    unsigned MBBNum = MBB->getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB->getNumber())) {
      if (++SCCVisited[C] != C->getSize())
        continue;
      else
        MBBNum = C->getNumber();
    }

    SWAttributes &Attr = Attrs[MBBNum];

    // If the element is used in the block, or if it is anticipated in all
    // successors it is also anticipated at the beginning, since we consider
    // entire blocks.
    //          -
    // ANTIN = | APP || ANTOUT
    //          -
    TargetResultSet &ANTINb = Attr.ANTIN;
    bool NewANTIN = UsesElt(MBBNum) || ANTOUT(Attrs, MBBNum, Elt);
    Assign(ANTINb, NewANTIN);
  }

  // Reuse the map.
  SCCVisited.clear();

  // RPO traversal for availability computation. We want to handle the SCC only
  // when we reach the *FIRST* component.
  for (const MachineBasicBlock *MBB : RPOT) {
    unsigned MBBNum = MBB->getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB->getNumber())) {
      if (++SCCVisited[C] != 1)
        continue;
      else
        MBBNum = C->getNumber();
    }

    SWAttributes &Attr = Attrs[MBBNum];

    // If the element is used in the block, or if it is always available in
    // all predecessors , it is also available on exit, since we consider
    // entire blocks.
    //          -
    // AVOUT = | APP || AVIN
    //          -
    TargetResultSet &AVOUTb = Attr.AVOUT;
    bool NewAVOUT = UsesElt(MBBNum) || AVIN(Attrs, MBBNum, Elt);
    Assign(AVOUTb, NewAVOUT);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  VERBOSE_DEBUG(dumpAttributes(Elt, Attrs));
#endif // LLVM_ENABLE_DUMP
}

bool ShrinkWrapper::hasCriticalEdges(unsigned Elt, AttributeMap &Attrs) {
  bool Needs = false;
  for (const MachineBasicBlock &MBB : MF) {
    bool IsSCCLoop = false;
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB.getNumber())) {
      // Skip all the blocks that are not the number of the SCC, since all the
      // attributes are based on that number.
      if (static_cast<unsigned>(MBB.getNumber()) != C->getNumber())
        continue;
      else
        IsSCCLoop = true;
    }

    unsigned MBBNum = blockNumber(MBB.getNumber());
    // If the block is never returning, we won't bother saving / restoring.
    if (NoReturnBlocks.test(MBBNum))
      continue;

    SWAttributes &Attr = Attrs[MBBNum];
    // Check if this block is ANTIN and has an incoming critical edge where it
    // is not ANTIN. If it's the case, mark it as used, and recompute.
    if (Attr.ANTIN.test(Elt)) {
      auto Preds = blockPredecessors(MBBNum);
      // We're looking for more than 2 predecessors. Also, if it's a SCCLoop, it
      // has a predecessor that is itself.
      if (std::distance(Preds.begin(), Preds.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *P : Preds) {
          unsigned PredNum = blockNumber(P->getNumber());
          SWAttributes &Attr = Attrs[PredNum];
          TargetResultSet &ANTINp = Attr.ANTIN;
          if (!ANTINp.test(Elt)) {
            // FIXME: ShrinkWrap2: emit remark.
            VERBOSE_DEBUG(dbgs()
                          << "Incoming critical edge in " << MBBNum << ".\n");
            // Mark it as used.
            TargetResultSet &Used = Uses[PredNum];
            if (Used.empty())
              Used.resize(SWI->getNumResultBits());
            Used.set(Elt);

            // Also, mark it as ANTIN and AVOUT, since we're not calling
            // populateAttributes anymore.
            ANTINp.set(Elt);
            Attr.AVOUT.set(Elt);
            Needs = true;
          }
        }
      }
    }
    // Check if this block is AVOUT and has an outgoing critical edge where it
    // is not AVOUT. If it's the case, mark it as used, and recompute.
    if (Attr.AVOUT.test(Elt)) {
      auto Succs = blockSuccessors(MBBNum);
      // We're looking for more than 2 successors. Also, if it's a SCCLoop, it
      // has a predecessor that is itself.
      if (std::distance(Succs.begin(), Succs.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *S : Succs) {
          unsigned SuccNum = blockNumber(S->getNumber());
          SWAttributes &Attr = Attrs[SuccNum];
          TargetResultSet &AVOUTs = Attr.AVOUT;
          if (!AVOUTs.test(Elt)) {
            // FIXME: ShrinkWrap2: emit remark.
            VERBOSE_DEBUG(dbgs()
                          << "Outgoing critical edge in " << MBBNum << ".\n");
            // Mark it as used.
            TargetResultSet &Used = Uses[SuccNum];
            if (Used.empty())
              Used.resize(SWI->getNumResultBits());
            Used.set(Elt);

            // Also, mark it as AVOUT and ANTIN, since we're not calling
            // populateAttrbutes anymore.
            AVOUTs.set(Elt);
            Attr.ANTIN.set(Elt);
            Needs = true;
          }
        }
      }
    }
  }
  // Recompute if needed.
  return Needs;
}

void ShrinkWrapper::gatherAttributesResults(unsigned Elt, AttributeMap &Attrs) {
  for (const MachineBasicBlock &MBB : MF) {
    bool IsSCCLoop = false;
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB.getNumber())) {
      // Skip all the blocks that are not the number of the SCC, since all the
      // attributes are based on that number.
      if (static_cast<unsigned>(MBB.getNumber()) != C->getNumber())
        continue;
      else
        IsSCCLoop = true;
    }

    unsigned MBBNum = blockNumber(MBB.getNumber());
    // If the block is never returning, we won't bother saving / restoring.
    if (NoReturnBlocks.test(MBBNum))
      continue;

    SWAttributes &Attr = Attrs[MBBNum];

    // If the uses are anticipated on *all* the paths leaving this block, and if
    // it is not available at the entry of this block (if it is, then it means
    // it has been saved already, but not restored), and if *none* of the
    // predecessors anticipates this element on their output (we want to get the
    // "highest" block), then we can identify a save point for the function.
    //
    // SAVE = ANTIN && !AVIN && !ANTIN(pred[i])
    //
    bool NS =
        none_of(blockPredecessors(MBBNum), [&](const MachineBasicBlock *P) {
          return Attrs[blockNumber(P->getNumber())].ANTIN.test(Elt);
        });
    if (NS && Attr.ANTIN.test(Elt) && !AVIN(Attrs, MBBNum, Elt)) {
      TargetResultSet &Save = Saves[MBBNum];
      if (Save.empty())
        Save.resize(SWI->getNumResultBits());
      Save.set(Elt);
    }

    // If the uses are available on *all* the paths leading to this block, and
    // if the element is not anticipated at the exit of this block (if it is,
    // then it means it has been restored already), and if *none* of the
    // successors make the element available (we want to cover the // deepest //
    // use), then we can identify a restrore point for the function.
    //
    // RESTORE = AVOUT && !ANTOUT && !AVOUT(succ[i])
    //
    bool NR = none_of(blockSuccessors(MBBNum), [&](const MachineBasicBlock *S) {
      return Attrs[blockNumber(S->getNumber())].AVOUT.test(Elt);
    });
    if (NR && Attr.AVOUT.test(Elt) && !ANTOUT(Attrs, MBBNum, Elt)) {
      TargetResultSet &Restore = Restores[MBBNum];
      if (Restore.empty())
        Restore.resize(SWI->getNumResultBits());
      Restore.set(Elt);
    }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ShrinkWrapper::dumpAttributes(unsigned Elt,
                                   const AttributeMap &Attrs) const {
  for (const MachineBasicBlock &MBB : MF) {
    unsigned MBBNum = MBB.getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
      if (MBBNum != C->getNumber())
        continue;
    const SWAttributes &Attr = Attrs[MBBNum];
    dbgs() << "BB#" << MBBNum << "<";
    SWI->printElt(Elt, dbgs());
    dbgs() << ">"
           << ":\n\tANTOUT : " << ANTOUT(Attrs, MBBNum, Elt) << '\n'
           << "\tANTIN : " << Attr.ANTIN.test(Elt) << '\n'
           << "\tAVIN : " << AVIN(Attrs, MBBNum, Elt) << '\n'
           << "\tAVOUT : " << Attr.AVOUT.test(Elt) << '\n';
  }
}
#endif // LLVM_ENABLE_DUMP

void ShrinkWrapper::postProcessResults() {
  // If there is only one use of the element, and multiple saves / restores,
  // remove them and place the save / restore at the used MBB's boundaries.
  for (unsigned Elt : AllElts.set_bits()) {
    auto HasElt = [&](const TargetResultSet &Res) {
      return Res.empty() ? false : Res.test(Elt);
    };
    auto Found1 = find_if(OriginalUses, HasElt);
    auto Found2 = Found1 == OriginalUses.end()
                      ? Found1
                      : std::find_if(std::next(Found1), OriginalUses.end(), HasElt);
    if (Found1 != OriginalUses.end() && Found2 == OriginalUses.end()) {
      // Gather all the saves.
      MBBSet SavesElt(MF.getNumBlockIDs());
      for (auto &KV : Saves) {
        unsigned MBBNum = KV.first;
        const TargetResultSet &Elts = KV.second;
        if (Elts.test(Elt))
          SavesElt.set(MBBNum);
      }

      // Gather all the restores.
      MBBSet RestoresElt(MF.getNumBlockIDs());
      for (auto &KV : Restores) {
        unsigned MBBNum = KV.first;
        const TargetResultSet &Elts = KV.second;
        if (Elts.test(Elt))
          RestoresElt.set(MBBNum);
      }

      // If we only have a single save and a single restore, keep it that way.
      if (SavesElt.count() == 1 && RestoresElt.count() == 1)
        continue;

      // Remove saves and restores from the maps.
      for (unsigned MBBNum : SavesElt.set_bits())
        Saves[MBBNum].reset(Elt);
      for (unsigned MBBNum : RestoresElt.set_bits())
        Restores[MBBNum].reset(Elt);

      // Add it to the unique block that uses it.
      unsigned MBBNum = std::distance(OriginalUses.begin(), Found1);
      for (auto *Map : {&Saves, &Restores}) {
        TargetResultSet &Elts = (*Map)[MBBNum];
        if (Elts.empty())
          Elts.resize(SWI->getNumResultBits());
        Elts.set(Elt);
      }
    }
  }

  // Remove all the empty entries from the Saves / Restores maps.
  // FIXME: ShrinkWrap2: Should we even have empty entries?
  SmallVector<SparseBBResultSetMap::iterator, 4> ToRemove;
  for (auto *Map : {&Saves, &Restores}) {
    for (auto It = Map->begin(), End = Map->end(); It != End; ++It)
      if (It->second.count() == 0)
        ToRemove.push_back(It);
    for (auto It : ToRemove)
      Map->erase(It);
    ToRemove.clear();
  }
}

unsigned ShrinkWrapper::computeShrinkWrappingCost(
    MachineBlockFrequencyInfo *MBFI) const {
  unsigned Cost = 0;
  for (const MachineBasicBlock &MBB : MF) {
    unsigned BlockCost = 0;
    for (auto *Map : {&Saves, &Restores}) {
      auto Found = Map->find(MBB.getNumber());
      if (Found != Map->end())
        BlockCost += Found->second.count();
    }
    auto Frequency =
        static_cast<double>(MBFI->getBlockFreq(&MBB).getFrequency()) /
        MBFI->getEntryFreq();
    Cost += BlockCost * Frequency * 100;
  }
  return Cost;
}

unsigned
ShrinkWrapper::computeDefaultCost(MachineBlockFrequencyInfo *MBFI) const {
  unsigned Cost = 0;
  for (const MachineBasicBlock &MBB : MF) {
    unsigned BlockCost =
        &MBB == &MF.front() || MBB.isReturnBlock() ? AllElts.count() : 0;
    auto Frequency =
        static_cast<double>(MBFI->getBlockFreq(&MBB).getFrequency()) /
        MBFI->getEntryFreq();
    Cost += BlockCost * Frequency * 100;
  }
  return Cost;
}

void ShrinkWrapper::verifySavesRestores() const {
  auto HasElt = [&](const SparseBBResultSetMap &Map, unsigned Elt) {
    return find_if(Map, [&](const std::pair<unsigned, TargetResultSet> &KV) {
             return KV.second.test(Elt);
           }) != Map.end();
  };

  auto RestoresElt = [&](unsigned Elt) { return HasElt(Restores, Elt); };
  auto SavesElt = [&](unsigned Elt) { return HasElt(Saves, Elt); };

  // Check that all the CSRs used in the function are saved at least once.
  for (unsigned Elt : AllElts.set_bits())
    if (!SavesElt(Elt) && !RestoresElt(Elt))
      llvm_unreachable("Used CSR is never saved!");

  // Check that there are no saves / restores in a loop.
  for (const SparseBBResultSetMap *Map : {&Saves, &Restores})
    for (auto &KV : *Map)
      if (SI.getSCCLoopFor(KV.first))
        llvm_unreachable("Save / restore in a loop.");

  // Keep track of the currently saved elements.
  TargetResultSet Saved(SWI->getNumResultBits());
  // Cache the state of each call, to avoid redundant checks.
  std::vector<SmallVector<TargetResultSet, 2>> Cache(MF.getNumBlockIDs());

  // Verify if:
  // * All the saves are restored.
  // * All the restores are related to a store.
  // * There are no nested stores.
  std::function<void(const MachineBasicBlock *)> verifySavesRestoresRec =
      [&](const MachineBasicBlock *MBB) {
        unsigned MBBNum = MBB->getNumber();
        // Don't even check no-return blocks.
        if (MBB->succ_empty() && !MBB->isReturnBlock()) {
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << " is an no-return\n");
          return;
        }

        SmallVectorImpl<TargetResultSet> &State = Cache[MBBNum];
        if (find(State, Saved) != State.end()) {
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << " already visited.\n");
          return;
        }

        State.push_back(Saved);

        VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Save ";
                      for (unsigned Elt
                           : Saved.set_bits()) {
                        SWI->printElt(Elt, dbgs());
                        dbgs() << " ";
                      } dbgs()
                      << '\n');

        const TargetResultSet &SavesMBB = Saves.lookup(MBBNum);
        const TargetResultSet &RestoresMBB = Restores.lookup(MBBNum);

        // Get the intersection of the currently saved elements and the
        // elements to be saved for this basic block. If the intersection is
        // not empty, it means we have nested saves for the same elements.
        TargetResultSet Intersection(SavesMBB);
        Intersection &= Saved;

        DEBUG(for (unsigned Elt
                   : Intersection.set_bits()) {
          SWI->printElt(Elt, dbgs());
          dbgs() << " is saved twice.\n";
        });

        assert(Intersection.count() == 0 &&
               "Nested saves for the same elements.");
        Intersection.reset();

        // Save the elements to be saved.
        for (unsigned Elt : SavesMBB.set_bits()) {
          Saved.set(Elt);
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Save ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }

        // If the intersection of the currently saved elements and the
        // elements to be restored for this basic block is not equal to the
        // restores, it means we are trying to restore something that is not
        // saved.
        Intersection = RestoresMBB;
        Intersection &= Saved;

        assert(Intersection.count() == RestoresMBB.count() &&
               "Not all restores are saved.");

        // Restore the elements to be restored.
        for (int Elt : RestoresMBB.set_bits()) {
          Saved.reset(Elt);
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Restore ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }

        if (MBB->succ_empty() && Saved.count() != 0)
          llvm_unreachable("Not all saves are restored.");

        // Using the current set of saved elements, walk all the successors
        // recursively.
        for (MachineBasicBlock *Succ : MBB->successors())
          verifySavesRestoresRec(Succ);

        // Restore the state prior of the function exit.
        for (unsigned Elt : RestoresMBB.set_bits()) {
          Saved.set(Elt);
          VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Save ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }
        for (unsigned Elt : SavesMBB.set_bits()) {
          Saved.reset(Elt);
          VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Restore ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }
      };

  verifySavesRestoresRec(&MF.front());
}

unsigned
ShrinkWrapper::numberOfUselessSaves() const {
  // Keep track of the currently saved elements.
  TargetResultSet Saved(SWI->getNumResultBits());
  unsigned UselessSaves = 0;
  TargetResultSet Used(SWI->getNumResultBits());
  DenseMap<unsigned,
           SmallVector<std::pair<TargetResultSet, TargetResultSet>, 2>>
      Cache;

  std::function<void(const MachineBasicBlock *)> numberOfUselessSavesRec =
      [&](const MachineBasicBlock *MBB) {
        unsigned MBBNum = MBB->getNumber();
        if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
          if (MBBNum != C->getNumber())
            return;

        auto& Vect = Cache[MBBNum];
        if (find_if(
                Vect,
                [&](const std::pair<TargetResultSet, TargetResultSet> &Elt) {
                  return Elt.first == Saved && Elt.second == Used;
                }) != Vect.end())
          return;
        Vect.emplace_back(Saved, Used);

        const TargetResultSet &SavesMBB = Saves.lookup(MBBNum);
        const TargetResultSet &RestoresMBB = Restores.lookup(MBBNum);

        // Save the elements to be saved.
        for (unsigned Elt : SavesMBB.set_bits()) {
          Saved.set(Elt);
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Save ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }

        auto& UsesHere = OriginalUses[MBBNum];
        Used |= UsesHere;

        VERBOSE_DEBUG(
            dbgs() << "BB#" << MBBNum << " Used: "; for (unsigned Elt
                                                         : Used.set_bits()) {
              SWI->printElt(Elt, dbgs());
              dbgs() << " ,";
            } dbgs() << '\n');

        // Restore the elements to be restored.
        for (int Elt : RestoresMBB.set_bits()) {
          if (!Used.test(Elt)) {
            VERBOSE_DEBUG(SWI->printElt(Elt, dbgs());
                          dbgs() << " is restored in BB#" << MBBNum
                                 << " but never used.\n");
            ++UselessSaves;
          }
          Saved.reset(Elt);
          Used.reset(Elt);
          VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Restore ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }

        auto SavedState = Saved;
        auto UsedState = Used;

        for (const MachineBasicBlock *Succ : blockSuccessors(MBBNum)) {
          numberOfUselessSavesRec(Succ);
        }

        Saved = SavedState;
        Used = UsedState;
      };

  numberOfUselessSavesRec(&MF.front());

  return UselessSaves;
}

void ShrinkWrapper::emitRemarks(MachineOptimizationRemarkEmitter *ORE,
                                MachineBlockFrequencyInfo *MBFI) const {
  unsigned Cost = computeShrinkWrappingCost(MBFI);
  unsigned DefaultCost = computeDefaultCost(MBFI);
  int Improvement = DefaultCost - Cost;
  if (Improvement < 0)
    return;
  MachineOptimizationRemarkAnalysis R(DEBUG_TYPE, "ShrinkWrapped", {},
                                      &MF.front());
  R << "Shrink-wrapped function with cost " << ore::NV("ShrinkWrapCost", Cost)
    << " which is " << ore::NV("ShrinkWrapCostImprovement", Improvement)
    << " better than "
    << ore::NV("OriginalShrinkWrapCost", DefaultCost)
    << ", during which attributes were recomputed "
    << ore::NV("ShrinkWrapRecomputed", AttributesRecomputed) << " times.";
  unsigned Useless = numberOfUselessSaves();
  if (Useless)
    R << "Found " << ore::NV("UselessSavesNum", Useless) << " useless saves.";
  ORE->emit(R);
}

bool ShrinkWrapper::areResultsInteresting(
    MachineBlockFrequencyInfo *MBFI) const {
  if (!hasUses())
    return false;
  if (Saves.size() == 1) { // If we have only one save,
    unsigned MBBNum = Saves.begin()->first;
    unsigned FrontMBBNum = MF.front().getNumber();
    const TargetResultSet &EltsSaved = Saves.begin()->second;
    if (MBBNum == FrontMBBNum   // and the save it's in the entry block,
        && EltsSaved == AllElts) { // and it saves *ALL* the CSRs
      DEBUG(dbgs() << "No shrink-wrapping performed, all saves in the entry "
                      "block.\n";);
      return false; // then it's not interesting.
    }
  }

  // If the cost with shrink wrapping is better than the default, use it.
  unsigned Cost = computeShrinkWrappingCost(MBFI);
  unsigned DefaultCost = computeDefaultCost(MBFI);
  if (Cost >= DefaultCost)
    DEBUG(dbgs() << "No shrink-wrapping performed. ShrinkWrapCost: " << Cost
                 << ", DefaultCost: " << DefaultCost << '\n');
  return Cost < DefaultCost;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ShrinkWrapper::dumpResults() const {
  for (unsigned MBBNum = 0; MBBNum < MF.getNumBlockIDs(); ++MBBNum) {
    if (Saves.count(MBBNum) || Restores.count(MBBNum)) {
      DEBUG(dbgs() << "BB#" << MBBNum << ": Saves: ");
      auto Save = Saves.lookup(MBBNum);
      DEBUG(for (unsigned Elt
                 : Save.set_bits()) {
        SWI->printElt(Elt, dbgs());
        dbgs() << ", ";
      });
      DEBUG(dbgs() << "| Restores: ");
      auto Restore = Restores.lookup(MBBNum);
      DEBUG(for (unsigned Elt
                 : Restore.set_bits()) {
        SWI->printElt(Elt, dbgs());
        dbgs() << ", ";
      });

      DEBUG(dbgs() << '\n');
    }
  }
}
#endif // LLVM_ENABLE_DUMP

ShrinkWrapper::ShrinkWrapper(const MachineFunction &MF)
    : ShrinkWrapper(
          MF,
          MF.getSubtarget().getFrameLowering()->createCSRShrinkWrapInfo(MF)) {}

ShrinkWrapper::ShrinkWrapper(const MachineFunction &MF,
                             std::unique_ptr<ShrinkWrapInfo> SW)
    : MF(MF), Uses(MF.getNumBlockIDs()), SWI(std::move(SW)), SI(MF) {
  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  VERBOSE_DEBUG(for (auto &SCC
                     : SI.SCCs) {
    dbgs() << "SCCLoop: " << SCC.getNumber() << "\n  Pred: ";
    for (auto *Pred : SCC.Predecessors)
      dbgs() << Pred->getNumber() << ", ";
    dbgs() << "\n  Succ: ";
    for (auto *Succ : SCC.Successors)
      dbgs() << Succ->getNumber() << ", ";
    dbgs() << '\n';
  });

  // FIXME: ShrinkWrap2: Remove. Call SWI directly.
  determineUses();
  if (!hasUses())
    return;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  DEBUG(dumpUses());
#endif // LLVM_ENABLE_DUMP

  // Don't bother saving if we know we're never going to return.
  removeUsesOnNoReturnPaths();
  // FIXME: ShrinkWrap2: Check if there are any modifications before printing.
  DEBUG(dbgs() << "**** After removing uses on no-return paths\n";);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  DEBUG(dumpUses());
#endif // LLVM_ENABLE_DUMP

  markUsesOutsideLoops();
  // FIXME: ShrinkWrap2: Check if there are any modifications before printing.
  DEBUG(dbgs() << "**** After marking uses inside loops\n";);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  DEBUG(dumpUses());
#endif // LLVM_ENABLE_DUMP

  // FIXME: ShrinkWrap2: Find a better way to avoid treating added CSRs the same
  // as original ones. This is needed for postProcessResults.
  // FIXME: ShrinkWrap2: Probably just save / restore once per block if there
  // is only one register from the beginning.
  OriginalUses = Uses;

  AllElts.resize(SWI->getNumResultBits());
  for (const auto &Use : Uses)
    AllElts |= Use;

  auto &EntryUses = Uses[MF.front().getNumber()];

  // Compute the dataflow attributes described by Fred C. Chow.
  AttributeMap Attrs;
  // Reserve + emplace_back to avoid copies of empty bitvectors..
  unsigned Max = MF.getNumBlockIDs();
  Attrs.reserve(Max);
  for (unsigned i = 0; i < Max; ++i)
    Attrs.emplace_back(*SWI);
  // For each register, compute the dataflow attributes.
  // FIXME: ShrinkWrap2: Compute all elements at once.
  ReversePostOrderTraversal<const MachineFunction *> RPOT(&MF);
  for (unsigned Elt : AllElts.set_bits()) {
    // If it's used in the entry block, don't even compute it. We know the
    // results already.
    if (!EntryUses.empty() && EntryUses.test(Elt))
      continue;
    // Compute the attributes.
    computeAttributes(Elt, Attrs, RPOT);

    // FIXME: ShrinkWrap2: Don't do this in a loop. Try to fix it all at once.
    // If we detected critical edges, compute again.
    while (hasCriticalEdges(Elt, Attrs)) {
      ++AttributesRecomputed;
      computeAttributes(Elt, Attrs, RPOT);
    }

    gatherAttributesResults(Elt, Attrs);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    VERBOSE_DEBUG(dumpResults());
#endif // LLVM_ENABLE_DUMP
  }

  VERBOSE_DEBUG(dbgs() << "**** Analysis results\n";);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  VERBOSE_DEBUG(dumpResults());
#endif // LLVM_ENABLE_DUMP

  if (!EntryUses.empty()) {
    Saves[MF.front().getNumber()] |= EntryUses;
    for (const MachineBasicBlock &MBB : MF) {
      // FIXME: ShrinkWrap2: EHFuncletEntry.
      if (MBB.isReturnBlock())
        Restores[MBB.getNumber()] |= EntryUses;
    }
  }
  postProcessResults();

  DEBUG(dbgs() << "**** Shrink-wrapping results\n");
  // FIXME: ShrinkWrap2: Check if there are any modifications before printing.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  DEBUG(dumpResults());
#endif // LLVM_ENABLE_DUMP

// FIXME: ShrinkWrap2: Remove NDEBUG.
#if !defined(NDEBUG) || defined(EXPENSIVE_CHECKS)
  verifySavesRestores();
#endif // EXPENSIVE_CHECKS
}
