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

// FIXME: ShrinkWrap2: Remove, debug.
static cl::opt<cl::boolOrDefault> ViewCFGDebug("shrink-wrap-view", cl::Hidden,
                                               cl::desc("view cfg"));

ShrinkWrapper::SCCLoopInfo::SCCLoopInfo(const MachineFunction &MF) {
  // Create the SCCLoops.
  for (auto I = scc_begin(&MF); !I.isAtEnd(); ++I) {
    // Skip non-loop SCCs.
    if (!I.hasLoop())
      continue;

    SCCs.emplace_back();
    // The SCCLoop number is the smallest basic block number in the SCC.
    unsigned Number =
        (*std::min_element(
             I->begin(), I->end(),
             [&](const MachineBasicBlock *A, const MachineBasicBlock *B) {
               return A->getNumber() < B->getNumber();
             }))
            ->getNumber();
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

void ShrinkWrapper::populateAttributes(AttributeMap &Attrs) const {
  // Reserve + emplace_back to avoid copies of empty bitvectors..
  unsigned Max = MF.getNumBlockIDs();
  Attrs.reserve(MF.getNumBlockIDs());
  for (unsigned i = 0; i < Max; ++i)
    Attrs.emplace_back(*SWI);

  for (auto &KV : Uses) {
    unsigned MBBNum = KV.first;
    const TargetResultSet &Set = KV.second;
    // Setting APP also affects ANTIN and AVOUT.
    // ANTIN = APP || ANTOUT
    Attrs[MBBNum].ANTIN |= Set;
    // AVOUT = APP || AVIN
    Attrs[MBBNum].AVOUT |= Set;
  }
}

void ShrinkWrapper::computeAttributes(unsigned Elt, AttributeMap &Attrs) const {
  auto UsesElt = [&](unsigned MBBNum) {
    auto Found = Uses.find(MBBNum);
    if (Found == Uses.end())
      return false;
    return Found->second.test(Elt);
  };

  auto Assign = [&](TargetResultSet &Set, bool New) {
    if (Set.test(Elt) != New) {
      Set.flip(Elt);
    }
  };

  // Count how many times we visited a SCCLoop.
  DenseMap<const SCCLoop *, unsigned> SCCVisited;

  // PO traversal for anticipation computation. We want to handle the SCC only
  // when  we reach the *LAST* component.
  for (const MachineBasicBlock *MBB : post_order(&MF)) {
    unsigned MBBNum = MBB->getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB->getNumber())) {
      if (++SCCVisited[C] != C->getSize())
        continue;
      else
        MBBNum = C->getNumber();
    }

    SWAttributes &Attr = Attrs[MBBNum];
    // If there is an use of this on *all* the paths starting from
    // this basic block, the element is anticipated at the end of this
    // block
    // (propagate the IN attribute of successors to possibly merge saves)
    //           -
    //          | *false*             if no successor.
    // ANTOUT = |
    //          | && ANTIN(succ[i])   otherwise.
    //           -
    TargetResultSet &ANTOUTb = Attr.ANTOUT;
    auto Successors = blockSuccessors(MBB->getNumber());
    if (Successors.begin() == Successors.end())
      Assign(ANTOUTb, false);
    else {
      bool A = all_of(Successors, [&](const MachineBasicBlock *S) {
        if (S == MBB) // Ignore self.
          return true;
        return Attrs[blockNumber(S->getNumber())].ANTIN.test(Elt);
      });
      Assign(ANTOUTb, A);
    }

    // If the element is used in the block, or if it is anticipated in all
    // successors it is also anticipated at the beginning, since we consider
    // entire blocks.
    //          -
    // ANTIN = | APP || ANTOUT
    //          -
    TargetResultSet &ANTINb = Attr.ANTIN;
    bool NewANTIN = UsesElt(MBBNum) || Attr.ANTOUT.test(Elt);
    Assign(ANTINb, NewANTIN);
  }

  SCCVisited.clear();

  // RPO traversal for availabiloty computation. We want to handle the SCC only
  // when we reach the *FIRST* component.
  ReversePostOrderTraversal<const MachineFunction *> RPOT(&MF);
  for (const MachineBasicBlock *MBB : RPOT) {
    unsigned MBBNum = MBB->getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB->getNumber())) {
      if (++SCCVisited[C] != 1)
        continue;
      else
        MBBNum = C->getNumber();
    }

    SWAttributes &Attr = Attrs[MBBNum];
    // If there is an use of this  on *all* the paths arriving in  this block,
    // then the element is available in this block (propagate the  out attribute
    // of predecessors to possibly merge restores).
    //         -
    //        | *false*             if no predecessor.
    // AVIN = |
    //        | && AVOUT(pred[i])   otherwise.
    //         -
    TargetResultSet &AVINb = Attr.AVIN;
    auto Predecessors = blockPredecessors(MBB->getNumber());
    if (Predecessors.begin() == Predecessors.end())
      Assign(AVINb, false);
    else {
      bool A = all_of(Predecessors, [&](const MachineBasicBlock *P) {
        if (P == MBB) // Ignore self.
          return true;
        return Attrs[blockNumber(P->getNumber())].AVOUT.test(Elt);
      });
      Assign(AVINb, A);
    }

    // If the element is used in the block, or if it is always available in
    // all predecessors , it is also available on exit, since we consider
    // entire blocks.
    //          -
    // AVOUT = | APP || AVIN
    //          -
    TargetResultSet &AVOUTb = Attr.AVOUT;
    bool NewAVOUT = UsesElt(MBBNum) || Attr.AVIN.test(Elt);
    Assign(AVOUTb, NewAVOUT);
  }

  VERBOSE_DEBUG(dumpAttributes(Elt, Attrs));
}

bool ShrinkWrapper::gatherAttributesResults(unsigned Elt, AttributeMap &Attrs) {
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

    bool Change = false;
    // Check if this block is ANTIN and has an incoming critical edge where it
    // is not ANTIN. If it's the case, mark it as used, and recompute.
    if (Attr.ANTIN.test(Elt)) {
      auto Preds = blockPredecessors(MBBNum);
      if (std::distance(Preds.begin(), Preds.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *P : Preds) {
          unsigned PredNum = blockNumber(P->getNumber());
          SWAttributes &Attr = Attrs[PredNum];
          TargetResultSet &ANTINp = Attr.ANTIN;
          if (!ANTINp.test(Elt)) {
            VERBOSE_DEBUG(dbgs()
                          << "Incoming critical edge in " << MBBNum << ".\n");
            ANTINp.set(Elt);
            Attr.AVOUT.set(Elt);
            TargetResultSet &Used = Uses[PredNum];
            if (Used.empty())
              Used.resize(SWI->getNumResultBits());
            Used.set(Elt);
            Change = true;
          }
        }
      }
    }
    // Same for outgoing critical edges.
    if (Attr.AVOUT.test(Elt)) {
      auto Succs = blockSuccessors(MBBNum);
      if (std::distance(Succs.begin(), Succs.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *S : Succs) {
          unsigned SuccNum = blockNumber(S->getNumber());
          SWAttributes &Attr = Attrs[SuccNum];
          TargetResultSet &AVOUTs = Attr.AVOUT;
          if (!AVOUTs.test(Elt)) {
            VERBOSE_DEBUG(dbgs()
                          << "Outgoing critical edge in " << MBBNum << ".\n");
            AVOUTs.set(Elt);
            Attr.ANTIN.set(Elt);
            TargetResultSet &Used = Uses[SuccNum];
            if (Used.empty())
              Used.resize(SWI->getNumResultBits());
            Used.set(Elt);
            Change = true;
          }
        }
      }
    }
    // Recompute if needed.
    if (Change)
      return false;

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
          if (P == &MBB) // Ignore self.
            return false;
          return Attrs[blockNumber(P->getNumber())].ANTIN.test(Elt);
        });
    if (Attr.ANTIN.test(Elt) && !Attr.AVIN.test(Elt) && NS) {
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
      if (S == &MBB) // Ignore self.
        return false;
      return Attrs[blockNumber(S->getNumber())].AVOUT.test(Elt);
    });
    if (Attr.AVOUT.test(Elt) && !Attr.ANTOUT.test(Elt) && NR) {
      TargetResultSet &Restore = Restores[MBBNum];
      if (Restore.empty())
        Restore.resize(SWI->getNumResultBits());
      Restore.set(Elt);
    }
  }
  return true;
}

void ShrinkWrapper::dumpAttributes(unsigned Elt, AttributeMap &Attrs) const {
  for (const MachineBasicBlock &MBB : MF) {
    unsigned MBBNum = MBB.getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
      if (MBBNum != C->getNumber())
        continue;
    SWAttributes &Attr = Attrs[MBBNum];
    dbgs() << "BB#" << MBBNum << "<";
    SWI->printElt(Elt, dbgs());
    dbgs() << ">"
           << ":\n\tANTOUT : " << Attr.ANTOUT.test(Elt) << '\n'
           << "\tANTIN : " << Attr.ANTIN.test(Elt) << '\n'
           << "\tAVIN : " << Attr.AVIN.test(Elt) << '\n'
           << "\tAVOUT : " << Attr.AVOUT.test(Elt) << '\n';
  }
}

ShrinkWrapper::ShrinkWrapper(const MachineFunction &MF)
    : MF(MF),
      SWI(MF.getSubtarget().getFrameLowering()->createShrinkWrapInfo(MF)),
      SI(MF) {
  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  if (ViewCFGDebug == cl::BOU_TRUE)
    MF.viewCFGOnly();

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

  determineUses();
  if (!hasUses())
    return;

  DEBUG(dumpUses());

  // Don't bother saving if we know we're never going to return.
  removeUsesOnNoReturnPaths();
  DEBUG(dbgs() << "**** After removing uses on no-return paths\n";);
  DEBUG(dumpUses());

  markUsesOutsideLoops();
  DEBUG(dbgs() << "**** After marking uses inside loops\n";);
  DEBUG(dumpUses());

  // FIXME: ShrinkWrap2: Find a better way to avoid treating added CSRs the same
  // as original ones. This is needed for postProcessResults.
  // FIXME: Probably just save / restore once per block if there is only one
  // register from the beginning.
  auto OldUses = Uses;

  Elts.resize(SWI->getNumResultBits());
  for (auto &KV : Uses)
    Elts |= KV.second;

  // Use this scope to get rid of the dataflow attributes after the
  // computation.
  {
    // Compute the dataflow attributes described by Fred C. Chow.
    AttributeMap Attrs;
    populateAttributes(Attrs);
    // For each register, compute the data flow attributes.
    for (unsigned Elt : Elts.bit_set()) {
      // FIXME: ShrinkWrap2: Avoid recomputing all the saves / restores.
      do {
        for (auto *Map : {&Saves, &Restores}) {
          for (auto &KV : *Map) {
            TargetResultSet &Elts = KV.second;
            Elts.reset(Elt);
          }
        }
        // Compute the attributes.
        computeAttributes(Elt, Attrs);
        // Gather the results.
      } while (!gatherAttributesResults(Elt, Attrs));
      VERBOSE_DEBUG(dumpResults());
    }
  }

  DEBUG(dbgs() << "**** Analysis results\n";);
  DEBUG(dumpResults());

  postProcessResults(OldUses);

  DEBUG(dbgs() << "**** Shrink-wrapping results\n");
  DEBUG(dumpResults());

// FIXME: ShrinkWrap2: Remove NDEBUG.
#if !defined(NDEBUG) || defined(EXPENSIVE_CHECKS)
  verifySavesRestores();
#endif // EXPENSIVE_CHECKS
}

void ShrinkWrapper::determineUses() {
  // FIXME: ShrinkWrap2: We do unnecessary copies here.
  for (const MachineBasicBlock &MBB : MF) {
    if (const BitVector *Use = SWI->getUses(MBB.getNumber())) {
      unsigned MBBNum = blockNumber(MBB.getNumber());
      Uses[MBBNum].resize(SWI->getNumResultBits());
      Uses[MBBNum] |= *Use;
    }
  }
}

void ShrinkWrapper::dumpUses() const {
  BBResultSetMap Sorted(MF.getNumBlockIDs());
  for (auto &KV : Uses)
    Sorted[KV.first] = KV.second;

  for (unsigned MBBNum = 0; MBBNum < Sorted.size(); ++MBBNum) {
    const TargetResultSet &Elts = Sorted[MBBNum];
    if (!Elts.count())
      continue;

    dbgs() << "BB#" << MBBNum << " uses : ";
    int Elt = Elts.find_first();
    if (Elt > 0)
      SWI->printElt(Elt, dbgs());
    for (Elt = Elts.find_next(Elt); Elt > 0; Elt = Elts.find_next(Elt)) {
      dbgs() << ", ";
      SWI->printElt(Elt, dbgs());
    }
    dbgs() << '\n';
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

  for (unsigned MBBNum : NoReturnBlocks.bit_set()) {
    DEBUG(dbgs() << "Remove uses from no-return BB#" << MBBNum << '\n');
    Uses.erase(MBBNum);
  }
}

void ShrinkWrapper::markUsesOutsideLoops() {
  // Keep track of the elements to attach to a basic block.
  SparseBBResultSetMap ToInsert;
  for (auto &KV : Uses) {
    unsigned MBBNum = KV.first;
    const TargetResultSet &Elts = KV.second;

    auto Mark = [&](const MachineBasicBlock *Block) {
      unsigned BlockNum = Block->getNumber();
      if (ToInsert[BlockNum].empty())
        ToInsert[BlockNum].resize(SWI->getNumResultBits());
      ToInsert[BlockNum] |= Elts;
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

void ShrinkWrapper::postProcessResults(const SparseBBResultSetMap &OldUses) {
  // If there is only one use of the element, and multiple saves / restores,
  // remove them and place the save / restore at the used MBB.
  for (unsigned Elt : Elts.bit_set()) {
    // FIXME: ShrinkWrap2: 2x std::find_if.
    unsigned UseCount =
        count_if(OldUses, [&](const std::pair<unsigned, TargetResultSet> &Res) {
          return Res.second.test(Elt);
        });
    if (UseCount == 1) {
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

      if (SavesElt.count() == 1 && RestoresElt.count() == 1)
        continue;

      for (unsigned MBBNum : SavesElt.bit_set())
        Saves[MBBNum].reset(Elt);
      for (unsigned MBBNum : RestoresElt.bit_set())
        Restores[MBBNum].reset(Elt);

      auto It = find_if(OldUses,
                        [&](const std::pair<unsigned, TargetResultSet> &Res) {
                          return Res.second.test(Elt);
                        });
      assert(It != OldUses.end() && "We are sure there is exactly one.");

      // Add it to the unique block that uses it.
      unsigned MBBNum = It->first;
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

void ShrinkWrapper::verifySavesRestores() const {
  auto HasElt = [&](const SparseBBResultSetMap &Map, unsigned Elt) {
    return find_if(Map, [&](const std::pair<unsigned, TargetResultSet> &KV) {
             return KV.second.test(Elt);
           }) != Map.end();
  };

  auto RestoresElt = [&](unsigned Elt) { return HasElt(Restores, Elt); };
  auto SavesElt = [&](unsigned Elt) { return HasElt(Saves, Elt); };

  // Check that all the CSRs used in the function are saved at least once.
  for (unsigned Elt : Elts.bit_set())
    assert(SavesElt(Elt) || RestoresElt(Elt) && "Used CSR is never saved!");

  // Check that there are no saves / restores in a loop.
  for (const SparseBBResultSetMap *Map : {&Saves, &Restores}) {
    for (auto &KV : *Map)
      assert(!SI.getSCCLoopFor(KV.first) && "Save / restore in a loop.");
  }

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
                           : Saved.bit_set()) {
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

        for (unsigned Elt : Intersection.bit_set())
          DEBUG(SWI->printElt(Elt, dbgs()); dbgs() << " is saved twice.\n");

        assert(Intersection.count() == 0 &&
               "Nested saves for the same elements.");
        Intersection.reset();

        // Save the elements to be saved.
        for (unsigned Elt : SavesMBB.bit_set()) {
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
        for (int Elt : RestoresMBB.bit_set()) {
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
        for (unsigned Elt : RestoresMBB.bit_set()) {
          Saved.set(Elt);
          VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Save ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }
        for (unsigned Elt : SavesMBB.bit_set()) {
          Saved.reset(Elt);
          VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Restore ";
                        SWI->printElt(Elt, dbgs()); dbgs() << ".\n");
        }
      };

  verifySavesRestoresRec(&MF.front());
}

void ShrinkWrapper::dumpResults() const {
  for (unsigned MBBNum = 0; MBBNum < MF.getNumBlockIDs(); ++MBBNum) {
    if (Saves.count(MBBNum) || Restores.count(MBBNum)) {
      DEBUG(dbgs() << "BB#" << MBBNum << ": Saves: ");
      auto Save = Saves.lookup(MBBNum);
      for (unsigned Elt : Save.bit_set())
        DEBUG(SWI->printElt(Elt, dbgs()); dbgs() << ", ");
      DEBUG(dbgs() << "| Restores: ");
      auto Restore = Restores.lookup(MBBNum);
      for (unsigned Elt : Restore.bit_set())
        DEBUG(SWI->printElt(Elt, dbgs()); dbgs() << ", ");

      DEBUG(dbgs() << '\n');
    }
  }
}
