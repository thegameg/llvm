//===-- ShrinkWrap2.cpp - Compute safe point for prolog/epilog insertion --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass is an improvement of the current shrink-wrapping pass, based, this
// time, on the dataflow analysis described in "Minimizing Register Usage
// Penalty at Procedure Calls - Fred C. Chow" [1]. The aim of this improvement
// is to remove the restriction that the current shrink-wrapping pass is having,
// which is having only one save / restore point for all the registers and the
// stack adjustment.
// FIXME: ShrinkWrap2: Random thoughts:
// - r193749 removed an old pass that was an implementation of [1].
// - Cost model: use MachineBlockFrequency and some instruction cost model?
// - Split critical edges on demand?
// - Eliminate trivial cases where most of the CSRs are used in the entry block?
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <set>

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCAsmInfo.h"

#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#include "ShrinkWrapMeasure.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"

// FIXME: ShrinkWrap2: Fix name.
#define DEBUG_TYPE "shrink-wrap2"

#define VERBOSE_DEBUG(X)                                                       \
  do {                                                                         \
    if (VerboseDebug)                                                          \
      DEBUG(X);                                                                \
  } while (0);

using namespace llvm;

// FIXME: ShrinkWrap2: Fix name.
static cl::opt<cl::boolOrDefault>
    EnableShrinkWrap2Opt("enable-shrink-wrap2", cl::Hidden,
                         cl::desc("enable the shrink-wrapping 2 pass"));

static cl::opt<cl::boolOrDefault>
    VerboseDebug("shrink-wrap-verbose", cl::Hidden,
                 cl::desc("verbose debug output"));

// FIXME: ShrinkWrap2: Remove, debug.
static cl::opt<cl::boolOrDefault> ViewCFGDebug("shrink-wrap-view", cl::Hidden,
                                               cl::desc("view cfg"));

namespace {

/// Iterator for successors / predecessors. This is here to work with
/// SmallVector and std::vector at the same time.
typedef const MachineBasicBlock *const *MBBIterator;

// FIXME: ShrinkWrap2: Fix name.
class ShrinkWrap2 : public MachineFunctionPass {
  typedef BitVector BBSet;
  typedef BitVector RegSet;
  // Idx = MBB.getNumber()
  typedef SmallVector<RegSet, 8> BBRegSetMap;
  typedef DenseMap<unsigned, RegSet> SparseBBRegSetMap;

  /// Store callee-saved-altering basic blocks.
  SparseBBRegSetMap UsedCSR;

  /// All the CSR used in the function. This is used for quicker iteration over
  /// the registers.
  /// FIXME: ShrinkWrap2: PEI needs this, instead of calling
  /// TII->determineCalleeSaves.
  RegSet Regs;
  RegSet TargetAddedRegs;
  RegSet TargetRemovedRegs;
  RegSet TargetSavedRegs;
  BBSet NoReturnBlocks;

  // FIXME: ShrinkWrap2: Explain anticipated / available and how the
  // properties are used.
  struct SWAttributes {
    /// Is the register anticipated at the end of this basic block?
    RegSet ANTOUT;
    /// Is the register anticipated at the beginning of this basic block?
    RegSet ANTIN;
    /// Is the register available at the beginning of this basic block?
    RegSet AVIN;
    /// Is the register available at the end of this basic block?
    RegSet AVOUT;

    SWAttributes(const MachineFunction &MF) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      unsigned NumRegs = TRI.getNumRegs();
      for (RegSet *Regs : {&ANTOUT, &ANTIN, &AVIN, &AVOUT})
        (*Regs).resize(NumRegs);
    }
  };

  /// An SCC that was discovered through the scc_iterator on the function. This
  /// is used in order to detect loops, reducible *AND* irreducible.
  struct SCCLoop {
    /// The successors of the SCC. There are blocks outside the SCC.
    SetVector<const MachineBasicBlock *,
              SmallVector<const MachineBasicBlock *, 4>>
        Successors;
    iterator_range<MBBIterator> successors() const {
      return {&*Successors.begin(), &*Successors.end()};
    }
    /// The predecessors of the SCC. There are blocks outside the SCC.
    SetVector<const MachineBasicBlock *,
              SmallVector<const MachineBasicBlock *, 4>>
        Predecessors;
    iterator_range<MBBIterator> predecessors() const {
      return {&*Predecessors.begin(), &*Predecessors.end()};
    }
    /// The SCC number. This number is the smallest basic block number int he
    /// SCC. This is used when we replace basic blocks with SCCs for loops.
    unsigned Number;
    unsigned getNumber() const { return Number; }
    /// The number of blocks the SCC contains.
    unsigned Size;
    unsigned getSize() const { return Size; }
  };

  /// Wrapper around scc_iterator that collects SCCs that are loops, computes
  /// their successor / predecessor and assigns an unique number based on the
  /// basic blocks it contains.
  struct SCCLoopInfo {
    /// Own the SCCs.
    SmallVector<SCCLoop, 4> SCCs;
    /// Map a basic block number to an SCCLoop number. The SCCLoop number is the
    /// position in the `SCCs` vector, and it is differrent from the SCC::Number
    /// attribute, which is the smallest basic block number in the SCC.
    DenseMap<unsigned, unsigned> MBBToSCC;

    /// Get the SCCLoop for a designated basic block number. If there is no
    /// SCCLoop associated, return `nullptr`.
    SCCLoop *getSCCLoopFor(unsigned MBBNum) {
      auto It = MBBToSCC.find(MBBNum);
      if (It == MBBToSCC.end())
        return nullptr;
      return &SCCs[It->second];
    }
    const SCCLoop *getSCCLoopFor(unsigned MBBNum) const {
      return const_cast<SCCLoopInfo *>(this)->getSCCLoopFor(MBBNum);
    }
    void clear() {
      SCCs.clear();
      MBBToSCC.clear();
    }
    /// Initialize the successors / predecessors of the SCCLoops.
    void init(const MachineFunction &MF);
  };

  /// The replacement for the MachineLoopInfo, that takes care of irreducible
  /// loops as well.
  SCCLoopInfo SI;

  typedef SmallVector<SWAttributes, 8> AttributeMap;

  /// Final results.
  SparseBBRegSetMap Saves;
  SparseBBRegSetMap Restores;

public:
  /// Emit remarks.
  MachineOptimizationRemarkEmitter *ORE;

private:
  /// Get the block number or the SCCLoop's number.
  unsigned blockNumber(unsigned MBBNum) const;

  /// Get the block successors or the SCCLoop exit blocks.
  iterator_range<MBBIterator> blockSuccessors(const MachineFunction &MF,
                                              unsigned MBBNum) const;

  /// Get the block predecessors or the SCCLoop's predecessors.
  iterator_range<MBBIterator> blockPredecessors(const MachineFunction &MF,
                                                unsigned MBBNum) const;

  /// Populate the attribute maps with trivial properties from the used
  /// registers.
  void populateAttributes(const MachineFunction &MF, AttributeMap &Attrs) const;
  /// Compute the attributes for one register.
  // FIXME: ShrinkWrap2: Don't do this per register.
  void computeAttributes(unsigned Reg, const MachineFunction &MF,
                         AttributeMap &Attrs) const;
  /// Save the results for this particular register.
  // FIXME: ShrinkWrap2: Don't do this per register.
  bool gatherAttributesResults(unsigned Reg, const MachineFunction &MF,
                               AttributeMap &Attrs);
  /// Dump the contents of the attributes.
  // FIXME: ShrinkWrap2: Don't do this per register.
  void dumpAttributes(unsigned Reg, const MachineFunction &MF,
                      AttributeMap &Attrs) const;

  /// Determine all the calee saved register used in this function.
  /// This fills out the Regs set, containing all the CSR used in the entire
  /// function, and fills the UsedCSR map, containing all the CSR used per
  /// basic block.
  /// We don't use the TargetFrameLowering::determineCalleeSaves function
  /// because we need to associate each usage of a CSR to the corresponding
  /// basic block.
  // FIXME: ShrinkWrap2: Target hook might add other callee saves.
  // FIXME: ShrinkWrap2: Should we add the registers added by the target in
  // the
  // entry / exit block(s) ?
  void determineCalleeSaves(MachineFunction &MF);
  /// Remove uses and fill NoReturnBlocks with the blocks that we know are not
  /// going to return from the function.
  void removeUsesOnNoReturnPaths(MachineFunction &MF);
  void dumpUsedCSR(const MachineFunction &MF) const;

  /// This algorithm relies on the fact that there are no critical edges.
  // FIXME: ShrinkWrap2: Get rid of this.
  bool splitCriticalEdges(MachineFunction &MF);

  /// Mark all the basic blocks around the loop (pred, succ) as used,
  /// if there is an usage of a CSR inside a loop. We want to avoid any save /
  /// restore operations in a loop.
  void markUsesOutsideLoops(MachineFunction &MF);

  /// * Verify if the results are better than obvious results, like:
  ///   * CSR used in a single MBB: only one save and one restore.
  /// * Remove empty entries from the Saves / Restores maps.
  // FIXME: ShrinkWrap2: This shouldn't happen, we better fix the algorithm
  // first.
  void postProcessResults(MachineFunction &MF,
                          const SparseBBRegSetMap &UsedCSR);

  /// Return the save / restore points to MachineFrameInfo, to be used by PEI.
  void returnToMachineFrame(MachineFunction &MF);

  /// Verify save / restore points by walking the CFG.
  /// This asserts if anything went wrong.
  // FIXME: ShrinkWrap2: Should we add a special flag for this?
  // FIXME: ShrinkWrap2: Expensive checks?
  void verifySavesRestores(MachineFunction &MF) const;

  /// Dump the final shrink-wrapping results.
  void dumpResults(MachineFunction &MF) const;

  /// \brief Initialize the pass for \p MF.
  void init(MachineFunction &MF) {
    SI.init(MF);
    MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  }

  /// Clear the function's state.
  void clear() {
    UsedCSR.clear();

    Regs.clear();
    TargetAddedRegs.clear();
    TargetRemovedRegs.clear();
    TargetSavedRegs.clear();
    NoReturnBlocks.clear();

    SI.clear();
    Saves.clear();
    Restores.clear();
  }

  /// \brief Check if shrink wrapping is enabled for this target and function.
  bool isShrinkWrapEnabled(const MachineFunction &MF);

  // FIXME: ShrinkWrap2: releaseMemory?

  // Measurements =============================================================
public:
  MachineBlockFrequencyInfo *MBFI;
  unsigned isSave(const MachineBasicBlock &MBB) {
    if (!MBB.getParent()->getFrameInfo().getShouldUseShrinkWrap2()) {
      if (&MBB == &MBB.getParent()->front())
        return TargetSavedRegs.count();
      return 0;
    }

    unsigned Extra = 0;
    if (&MBB == &MBB.getParent()->front()) {
      auto Bits = TargetAddedRegs;
      Bits &= TargetRemovedRegs;
      Extra += Bits.count();
    }
    auto Found = Saves.find(MBB.getNumber());
    if (Found == Saves.end())
      return Extra;
    auto Bits = Found->second;
    Bits &= TargetRemovedRegs;
    return Extra + Bits.count();
  }

  unsigned isRestore(const MachineBasicBlock &MBB) {
    if (!MBB.getParent()->getFrameInfo().getShouldUseShrinkWrap2()) {
      for (auto &Ret : *MBB.getParent())
        if (Ret.isReturnBlock() && &MBB == &Ret)
          return TargetSavedRegs.count();
      return 0;
    }

    unsigned Extra = 0;
    for (auto &Ret : *MBB.getParent()) {
      if (Ret.isReturnBlock() && &MBB == &Ret) {
        auto Bits = TargetAddedRegs;
        Bits &= TargetRemovedRegs;
        Extra += Bits.count();
      }
    }

    auto Found = Restores.find(MBB.getNumber());
    if (Found == Restores.end())
      return Extra;
    auto Bits = Found->second;
    Bits &= TargetRemovedRegs;
    return Extra + Bits.count();
  }

public:
  static char ID;

  ShrinkWrap2() : MachineFunctionPass(ID) {
    initializeShrinkWrap2Pass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addRequired<MachineOptimizationRemarkEmitterPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "Shrink Wrapping analysis"; }

  /// \brief Perform the shrink-wrapping analysis and update
  /// the MachineFrameInfo attached to \p MF with the results.
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // End anonymous namespace.

char ShrinkWrap2::ID = 0;
char &llvm::ShrinkWrap2ID = ShrinkWrap2::ID;

// FIXME: ShrinkWrap2: Fix name.
INITIALIZE_PASS_BEGIN(ShrinkWrap2, "shrink-wrap2", "Shrink Wrap Pass", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
// FIXME: ShrinkWrap2: Fix name.
INITIALIZE_PASS_END(ShrinkWrap2, "shrink-wrap2", "Shrink Wrap Pass", false,
                    false)

void ShrinkWrap2::SCCLoopInfo::init(const MachineFunction &MF) {
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

unsigned ShrinkWrap2::blockNumber(unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return C->getNumber();
  return MBBNum;
}

iterator_range<MBBIterator>
ShrinkWrap2::blockSuccessors(const MachineFunction &MF, unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return {C->Successors.begin(), C->Successors.end()};
  const MachineBasicBlock *MBB = MF.getBlockNumbered(MBBNum);
  return {&*MBB->succ_begin(), &*MBB->succ_end()};
}

iterator_range<MBBIterator>
ShrinkWrap2::blockPredecessors(const MachineFunction &MF,
                               unsigned MBBNum) const {
  if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
    return {C->Predecessors.begin(), C->Predecessors.end()};
  const MachineBasicBlock *MBB = MF.getBlockNumbered(MBBNum);
  return {&*MBB->pred_begin(), &*MBB->pred_end()};
}

void ShrinkWrap2::populateAttributes(const MachineFunction &MF,
                                     AttributeMap &Attrs) const {
  // Reserve + emplace_back to avoid copies of empty bitsets..
  Attrs.reserve(MF.getNumBlockIDs());
  for (unsigned i = 0; i < MF.getNumBlockIDs(); ++i)
    Attrs.emplace_back(MF);

  for (auto &KV : UsedCSR) {
    unsigned MBBNum = KV.first;
    const RegSet &Regs = KV.second;
    // Setting APP also affects ANTIN and AVOUT.
    // ANTIN = APP || ANTOUT
    Attrs[MBBNum].ANTIN |= Regs;
    // AVOUT = APP || AVIN
    Attrs[MBBNum].AVOUT |= Regs;
  }
}

void ShrinkWrap2::computeAttributes(unsigned Reg, const MachineFunction &MF,
                                    AttributeMap &Attrs) const {
  // FIXME: ShrinkWrap2: Just set.
  auto AssignIfDifferent = [&](RegSet &Regs, bool New) {
    if (Regs.test(Reg) != New) {
      Regs.flip(Reg);
    }
  };
  auto UsesReg = [&](unsigned MBBNum) {
    auto Found = UsedCSR.find(MBBNum);
    if (Found == UsedCSR.end())
      return false;
    return Found->second.test(Reg);
  };

  DenseMap<const SCCLoop *, unsigned> SCCVisited;

  for (const MachineBasicBlock *MBB : post_order(&MF)) {
    unsigned MBBNum = MBB->getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB->getNumber())) {
      if (++SCCVisited[C] != C->getSize())
        continue;
      else
        MBBNum = C->getNumber();
    }

    SWAttributes &Attr = Attrs[MBBNum];
    // If there is an use of this register on *all* the paths starting from
    // this basic block, the register is anticipated at the end of this
    // block
    // (propagate the IN attribute of successors to possibly merge saves)
    //           -
    //          | *false*             if no successor.
    // ANTOUT = |
    //          | && ANTIN(succ[i])   otherwise.
    //           -
    RegSet &ANTOUTb = Attr.ANTOUT;
    auto Successors = blockSuccessors(MF, MBB->getNumber());
    if (Successors.begin() == Successors.end())
      AssignIfDifferent(ANTOUTb, false);
    else {
      bool A = all_of(Successors, [&](const MachineBasicBlock *S) {
        if (S == MBB) // Ignore self.
          return true;
        return Attrs[blockNumber(S->getNumber())].ANTIN.test(Reg);
      });
      AssignIfDifferent(ANTOUTb, A);
    }

    // If the register is used in the block, or if it is anticipated in all
    // successors it is also anticipated at the beginning, since we consider
    // entire blocks.
    //          -
    // ANTIN = | APP || ANTOUT
    //          -
    RegSet &ANTINb = Attr.ANTIN;
    bool NewANTIN = UsesReg(MBBNum) || Attr.ANTOUT.test(Reg);
    AssignIfDifferent(ANTINb, NewANTIN);
  }

  SCCVisited.clear();

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
    // If there is an use of this register on *all* the paths arriving in
    // this
    // block, then the register is available in this block (propagate the
    // out
    // attribute of predecessors to possibly merge restores).
    //         -
    //        | *false*             if no predecessor.
    // AVIN = |
    //        | && AVOUT(pred[i])   otherwise.
    //         -
    RegSet &AVINb = Attr.AVIN;
    auto Predecessors = blockPredecessors(MF, MBB->getNumber());
    if (Predecessors.begin() == Predecessors.end())
      AssignIfDifferent(AVINb, false);
    else {
      bool A = all_of(Predecessors, [&](const MachineBasicBlock *P) {
        if (P == MBB) // Ignore self.
          return true;
        return Attrs[blockNumber(P->getNumber())].AVOUT.test(Reg);
      });
      AssignIfDifferent(AVINb, A);
    }

    // If the register is used in the block, or if it is always available in
    // all predecessors , it is also available on exit, since we consider
    // entire blocks.
    //          -
    // AVOUT = | APP || AVIN
    //          -
    RegSet &AVOUTb = Attr.AVOUT;
    bool NewAVOUT = UsesReg(MBBNum) || Attr.AVIN.test(Reg);
    AssignIfDifferent(AVOUTb, NewAVOUT);
  }

  VERBOSE_DEBUG(dumpAttributes(Reg, MF, Attrs));
}

bool ShrinkWrap2::gatherAttributesResults(unsigned Reg,
                                          const MachineFunction &MF,
                                          AttributeMap &Attrs) {
  for (const MachineBasicBlock &MBB : MF) {
    bool IsSCCLoop = false;
    if (const SCCLoop *C = SI.getSCCLoopFor(MBB.getNumber())) {
      if (static_cast<unsigned>(MBB.getNumber()) != C->getNumber())
        continue;
      else
        IsSCCLoop = true;
    }

    unsigned MBBNum = blockNumber(MBB.getNumber());
    if (NoReturnBlocks.test(MBBNum))
      continue;
    SWAttributes &Attr = Attrs[MBBNum];
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

    bool Change = false;
    // Check if this block is ANTIN and has an incoming critical edge where it
    // is not ANTIN.
    if (Attr.ANTIN.test(Reg)) {
      auto Preds = blockPredecessors(MF, MBBNum);
      if (std::distance(Preds.begin(), Preds.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *P : Preds) {
          unsigned PredNum = blockNumber(P->getNumber());
          SWAttributes &Attr = Attrs[PredNum];
          RegSet &ANTINp = Attr.ANTIN;
          if (!ANTINp.test(Reg)) {
            VERBOSE_DEBUG(dbgs() << "Incoming critical edge in " << MBBNum
                                 << ".\n");
            ANTINp.set(Reg);
            Attr.AVOUT.set(Reg);
            RegSet &Used = UsedCSR[PredNum];
            if (Used.empty())
              Used.resize(TRI.getNumRegs());
            Used.set(Reg);
            Change = true;
          }
        }
      }
    }
    if (Attr.AVOUT.test(Reg)) {
      auto Succs = blockSuccessors(MF, MBBNum);
      if (std::distance(Succs.begin(), Succs.end()) >= 2 || IsSCCLoop) {
        for (const MachineBasicBlock *S : Succs) {
          unsigned SuccNum = blockNumber(S->getNumber());
          SWAttributes &Attr = Attrs[SuccNum];
          RegSet &AVOUTs = Attr.AVOUT;
          if (!AVOUTs.test(Reg)) {
            VERBOSE_DEBUG(dbgs() << "Outgoing critical edge in " << MBBNum
                                 << ".\n");
            AVOUTs.set(Reg);
            Attr.ANTIN.set(Reg);
            RegSet &Used = UsedCSR[SuccNum];
            if (Used.empty())
              Used.resize(TRI.getNumRegs());
            Used.set(Reg);
            Change = true;
          }
        }
      }
    }
    if (Change)
      return false;

    // If the register uses are anticipated on *all* the paths leaving this
    // block, and if the register is not available at the entrance of this
    // block
    // (if it is, then it means it has been saved already, but not
    // restored),
    // and if *none* of the predecessors anticipates this register on their
    // output (we want to get the "highest" block), then we can identify a
    // save
    // point for the function.
    //
    // SAVE = ANTIN && !AVIN && !ANTIN(pred[i])
    //
    bool NS =
        none_of(blockPredecessors(MF, MBBNum), [&](const MachineBasicBlock *P) {
          if (P == &MBB) // Ignore self.
            return false;
          return Attrs[blockNumber(P->getNumber())].ANTIN.test(Reg);
        });
    if (Attr.ANTIN.test(Reg) && !Attr.AVIN.test(Reg) && NS) {
      RegSet &Save = Saves[MBBNum];
      if (Save.empty())
        Save.resize(TRI.getNumRegs());
      Save.set(Reg);
    }

    // If the register uses are available on *all* the paths leading to this
    // block, and if the register is not anticipated at the exit of this
    // block
    // (if it is, then it means it has been restored already), and if *none*
    // of
    // the successors make the register available (we want to cover the
    // deepest
    // use), then we can identify a restrore point for the function.
    //
    // RESTORE = AVOUT && !ANTOUT && !AVOUT(succ[i])
    //
    bool NR =
        none_of(blockSuccessors(MF, MBBNum), [&](const MachineBasicBlock *S) {
          if (S == &MBB) // Ignore self.
            return false;
          return Attrs[blockNumber(S->getNumber())].AVOUT.test(Reg);
        });
    if (Attr.AVOUT.test(Reg) && !Attr.ANTOUT.test(Reg) && NR) {
      RegSet &Restore = Restores[MBBNum];
      if (Restore.empty())
        Restore.resize(TRI.getNumRegs());
      Restore.set(Reg);
    }
  }
  return true;
}
void ShrinkWrap2::dumpAttributes(unsigned Reg, const MachineFunction &MF,
                                 AttributeMap &Attrs) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  for (const MachineBasicBlock &MBB : MF) {
    unsigned MBBNum = MBB.getNumber();
    if (const SCCLoop *C = SI.getSCCLoopFor(MBBNum))
      if (MBBNum != C->getNumber())
        continue;
    SWAttributes &Attr = Attrs[MBBNum];
    dbgs() << "BB#" << MBBNum << "<" << PrintReg(Reg, &TRI) << ">"
           << ":\n\tANTOUT : " << Attr.ANTOUT.test(Reg) << '\n'
           << "\tANTIN : " << Attr.ANTIN.test(Reg) << '\n'
           << "\tAVIN : " << Attr.AVIN.test(Reg) << '\n'
           << "\tAVOUT : " << Attr.AVOUT.test(Reg) << '\n';
  }
}

// FIXME: ShrinkWrap2: Target hook might add other callee saves.
void ShrinkWrap2::determineCalleeSaves(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Walk all the uses of each callee-saved register, and map them to their
  // basic blocks.
  const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();

  // FIXME: ShrinkWrap2: Naked functions.
  // FIXME: ShrinkWrap2: __builtin_unwind_init.

  RegSet Regs(TRI.getNumRegs());

  // Test all the target's callee saved registers.
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];

    // Check for regmasks.
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isRegMask() && MO.clobbersPhysReg(Reg)) {
            RegSet &Used = UsedCSR[blockNumber(MBB.getNumber())];
            if (Used.empty())
              Used.resize(TRI.getNumRegs());
            Used.set(Reg);
            Regs.set(Reg);
          }
        }
      }
    }

    // If at least one of the aliases is used, mark the original register as
    // used.
    for (MCRegAliasIterator AliasReg(Reg, &TRI, true); AliasReg.isValid();
         ++AliasReg) {
      // Walk all the uses, excepting for debug instructions.
      for (auto MOI = MRI.reg_nodbg_begin(*AliasReg), e = MRI.reg_nodbg_end();
           MOI != e; ++MOI) {
        // Get or create the registers used for the BB.
        RegSet &Used =
            UsedCSR[blockNumber(MOI->getParent()->getParent()->getNumber())];
        // Resize if it's the first time used.
        if (Used.empty())
          Used.resize(TRI.getNumRegs());
        Used.set(Reg);
        Regs.set(Reg);
        // If it's a terminator, mark the successors as used as well, since
        // we can't save after a terminator (i.e. cbz w23, #10).
        if (MOI->getParent()->isTerminator()) {
          for (MachineBasicBlock *Succ :
               MOI->getParent()->getParent()->successors()) {
            RegSet &Used = UsedCSR[blockNumber(Succ->getNumber())];
            if (Used.empty())
              Used.resize(TRI.getNumRegs());
            Used.set(Reg);
          }
        }
      }
      // Look for any live-ins in basic blocks.
      for (const MachineBasicBlock &MBB : MF) {
        if (MBB.isLiveIn(*AliasReg)) {
          RegSet &Used = UsedCSR[blockNumber(MBB.getNumber())];
          // Resize if it's the first time used.
          if (Used.empty())
            Used.resize(TRI.getNumRegs());
          Used.set(Reg);
          Regs.set(Reg);
        }
      }
    }
  }

  // Now, we're going to determine which registers are added / removed by the
  // target itself.
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  std::unique_ptr<RegScavenger> RS(
      TRI.requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr);
  // FIXME: ShrinkWrap2: RegScavenger.
  TFI->determineCalleeSaves(MF, TargetSavedRegs, RS.get());
  TargetAddedRegs = Regs;
  TargetAddedRegs.flip();
  TargetAddedRegs &= TargetSavedRegs;
  TargetRemovedRegs = TargetSavedRegs;
  TargetRemovedRegs.flip();
  TargetRemovedRegs &= Regs;
  TargetRemovedRegs.flip();
}

void ShrinkWrap2::removeUsesOnNoReturnPaths(MachineFunction &MF) {
  NoReturnBlocks.resize(MF.getNumBlockIDs());

  // Mark all reachable blocks from any return blocks.
  for (const MachineBasicBlock &MBB : MF)
    if (MBB.isReturnBlock())
      for (const MachineBasicBlock *Block : inverse_depth_first(&MBB))
        NoReturnBlocks.set(Block->getNumber());

  // Flip, so that we can get the non-reachable blocks.
  NoReturnBlocks.flip();

  for (int MBBNum : NoReturnBlocks.bit_set()) {
    DEBUG(dbgs() << "Remove uses from no-return BB#" << MBBNum << '\n');
    UsedCSR.erase(MBBNum);
  }
}

void ShrinkWrap2::dumpUsedCSR(const MachineFunction &MF) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  BBRegSetMap Sorted(MF.getNumBlockIDs());
  for (auto &KV : UsedCSR)
    Sorted[KV.first] = KV.second;

  for (unsigned MBBNum = 0; MBBNum < Sorted.size(); ++MBBNum) {
    const RegSet &Regs = Sorted[MBBNum];
    if (Regs.empty())
      continue;

    dbgs() << "BB#" << MBBNum << " uses : ";
    int Reg = Regs.find_first();
    if (Reg > 0)
      dbgs() << PrintReg(Reg, &TRI);
    for (Reg = Regs.find_next(Reg); Reg > 0; Reg = Regs.find_next(Reg))
      dbgs() << ", " << PrintReg(Reg, &TRI);
    dbgs() << '\n';
  }
}

void ShrinkWrap2::postProcessResults(MachineFunction &MF,
                                     const SparseBBRegSetMap &UsedCSR) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  // If there is only one use of the register, and multiple saves / restores,
  // remove them and place the save / restore at the used MBB.
  for (int Reg : Regs.bit_set()) {
    // FIXME: ShrinkWrap2: 2x std::find_if.
    unsigned Uses =
        count_if(UsedCSR, [&](const std::pair<unsigned, RegSet> &CSR) {
          return CSR.second.test(Reg);
        });
    if (Uses == 1) {
      // Gather all the saves.
      BBSet SavesReg(MF.getNumBlockIDs());
      for (auto &KV : Saves) {
        unsigned MBBNum = KV.first;
        const RegSet &Regs = KV.second;
        if (Regs.test(Reg))
          SavesReg.set(MBBNum);
      }

      // Gather all the restores.
      BBSet RestoresReg(MF.getNumBlockIDs());
      for (auto &KV : Restores) {
        unsigned MBBNum = KV.first;
        const RegSet &Regs = KV.second;
        if (Regs.test(Reg))
          RestoresReg.set(MBBNum);
      }

      if (SavesReg.count() == 1 && RestoresReg.count() == 1)
        continue;

      for (int MBBNum : SavesReg.bit_set())
        Saves[MBBNum].reset(Reg);
      for (int MBBNum : RestoresReg.bit_set())
        Restores[MBBNum].reset(Reg);

      auto It = find_if(UsedCSR, [&](const std::pair<unsigned, RegSet> &CSR) {
        return CSR.second.test(Reg);
      });
      assert(It != UsedCSR.end() && "We are sure there is exactly one.");

      // Add it to the unique block that uses it.
      unsigned MBBNum = It->first;
      for (auto *Map : {&Saves, &Restores}) {
        RegSet &Regs = (*Map)[MBBNum];
        if (Regs.empty())
          Regs.resize(TRI.getNumRegs());
        Regs.set(Reg);
      }
    }
  }

  // Remove all the empty entries from the Saves / Restores maps.
  // FIXME: ShrinkWrap2: Should we even have empty entries?
  SmallVector<SparseBBRegSetMap::iterator, 4> ToRemove;
  for (auto *Map : {&Saves, &Restores}) {
    for (auto It = Map->begin(), End = Map->end(); It != End; ++It)
      if (It->second.count() == 0)
        ToRemove.push_back(It);
    for (auto It : ToRemove)
      Map->erase(It);
    ToRemove.clear();
  }
}

void ShrinkWrap2::returnToMachineFrame(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto Transform = [&](SparseBBRegSetMap &Src,
                       MachineFrameInfo::CalleeSavedMap &Dst) {
    for (auto &KV : Src) {
      MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
      RegSet &Regs = KV.second;
      std::vector<CalleeSavedInfo> &CSI = Dst[MBB];

      for (int Reg : Regs.bit_set())
        CSI.emplace_back(Reg);
    }
  };
  MFI.setShouldUseShrinkWrap2(true);
  Transform(Saves, MFI.getSaves());
  Transform(Restores, MFI.getRestores());
}

void ShrinkWrap2::verifySavesRestores(MachineFunction &MF) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto HasReg = [&](const SparseBBRegSetMap &Map, unsigned Reg) {
    return find_if(Map, [&](const std::pair<unsigned, RegSet> &KV) {
             return KV.second.test(Reg);
           }) != Map.end();

  };

  auto RestoresReg = [&](unsigned Reg) { return HasReg(Restores, Reg); };
  auto SavesReg = [&](unsigned Reg) { return HasReg(Saves, Reg); };

  // Check that all the CSRs used in the function are saved at least once.
  for (int Reg : Regs.bit_set())
    assert(SavesReg(Reg) || RestoresReg(Reg) && "Used CSR is never saved!");

  // Check that there are no saves / restores in a loop.
  for (const SparseBBRegSetMap *Map : {&Saves, &Restores}) {
    for (auto &KV : *Map)
      assert(!SI.getSCCLoopFor(KV.first) && "Save / restore in a loop.");
  }

  // Keep track of the currently saved registers.
  RegSet Saved(TRI.getNumRegs());
  // Cache the state of each call, to avoid redundant checks.
  std::vector<SmallVector<RegSet, 2>> Cache(MF.getNumBlockIDs());

  // Verify if:
  // * All the saves are restored.
  // * All the restores are related to a store.
  // * There are no nested stores.
  std::function<void(MachineBasicBlock *)> verifySavesRestoresRec = [&](
      MachineBasicBlock *MBB) {
    unsigned MBBNum = MBB->getNumber();
    // Don't even check no-return blocks.
    if (MBB->succ_empty() && !MBB->isReturnBlock()) {
      VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << " is an no-return\n");
      return;
    }

    SmallVectorImpl<RegSet> &State = Cache[MBBNum];
    if (find(State, Saved) != State.end()) {
      VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << " already visited.\n");
      return;
    }

    State.push_back(Saved);

    VERBOSE_DEBUG(
        dbgs() << "IN: BB#" << MBBNum << ": Save "; for (int Reg
                                                         : Saved.bit_set()) {
          dbgs() << PrintReg(Reg, &TRI) << " ";
        } dbgs() << '\n');

    const RegSet &SavesMBB = Saves.lookup(MBBNum);
    const RegSet &RestoresMBB = Restores.lookup(MBBNum);

    // Get the intersection of the currently saved registers and the
    // registers to be saved for this basic block. If the intersection is
    // not empty, it means we have nested saves for the same register.
    RegSet Intersection(SavesMBB);
    Intersection &= Saved;

    for (int Reg : Intersection.bit_set())
      DEBUG(dbgs() << PrintReg(Reg, &TRI) << " is saved twice.\n");

    assert(Intersection.count() == 0 && "Nested saves for the same register.");
    Intersection.reset();

    // Save the registers to be saved.
    for (int Reg : SavesMBB.bit_set()) {
      Saved.set(Reg);
      VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Save "
                           << PrintReg(Reg, &TRI) << ".\n");
    }

    // If the intersection of the currently saved registers and the
    // registers to be restored for this basic block is not equal to the
    // restores, it means we are trying to restore something that is not
    // saved.
    Intersection = RestoresMBB;
    Intersection &= Saved;

    assert(Intersection.count() == RestoresMBB.count() &&
           "Not all restores are saved.");

    // Restore the registers to be restored.
    for (int Reg : RestoresMBB.bit_set()) {
      Saved.reset(Reg);
      VERBOSE_DEBUG(dbgs() << "IN: BB#" << MBBNum << ": Restore "
                           << PrintReg(Reg, &TRI) << ".\n");
    }

    if (MBB->succ_empty() && Saved.count() != 0)
      llvm_unreachable("Not all saves are restored.");

    // Using the current set of saved registers, walk all the successors
    // recursively.
    for (MachineBasicBlock *Succ : MBB->successors())
      verifySavesRestoresRec(Succ);

    // Restore the state prior of the function exit.
    for (int Reg : RestoresMBB.bit_set()) {
      Saved.set(Reg);
      VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Save "
                           << PrintReg(Reg, &TRI) << ".\n");
    }
    for (int Reg : SavesMBB.bit_set()) {
      Saved.reset(Reg);
      VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Restore "
                           << PrintReg(Reg, &TRI) << ".\n");
    }
  };

  verifySavesRestoresRec(&MF.front());
}

bool ShrinkWrap2::isShrinkWrapEnabled(const MachineFunction &MF) {
  auto BecauseOf = [&](const char *Title, DebugLoc Loc = {}) {
    MachineOptimizationRemarkMissed R("shrink-wrap", Title, Loc, &MF.front());
    ORE->emit(R);
    return false;
  };

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrap2Opt) {
  case cl::BOU_UNSET: {
    if (!TFI->enableShrinkWrapping(MF))
      return BecauseOf("TargetDisabled");
    // Windows with CFI has some limitations that make it impossible
    // to use shrink-wrapping.
    if (MF.getTarget().getMCAsmInfo()->usesWindowsCFI())
      return BecauseOf("WindowsCFI");

    // Sanitizers look at the value of the stack at the location
    // of the crash. Since a crash can happen anywhere, the
    // frame must be lowered before anything else happen for the
    // sanitizers to be able to get a correct stack frame.
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeAddress))
      return BecauseOf("asan");
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeThread))
      return BecauseOf("tsan");
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeMemory))
      return BecauseOf("msan");
  }
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}

void ShrinkWrap2::dumpResults(MachineFunction &MF) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // for (MachineBasicBlock &MBB : MF) {
  for (unsigned MBBNum = 0; MBBNum < MF.getNumBlockIDs(); ++MBBNum) {
    if (Saves.count(MBBNum) || Restores.count(MBBNum)) {
      DEBUG(dbgs() << "BB#" << MBBNum << ": Saves: ");
      auto Save = Saves.lookup(MBBNum);
      for (int Reg : Save.bit_set())
        DEBUG(dbgs() << PrintReg(Reg, &TRI) << ", ");
      DEBUG(dbgs() << "| Restores: ");
      auto Restore = Restores.lookup(MBBNum);
      for (int Reg : Restore.bit_set())
        DEBUG(dbgs() << PrintReg(Reg, &TRI) << ", ");

      DEBUG(dbgs() << '\n');
    }
  }
}

bool ShrinkWrap2::splitCriticalEdges(MachineFunction &MF) {
  auto IsCriticalEdge = [&](MachineBasicBlock *Src, MachineBasicBlock *Dst) {
    return Src->succ_size() > 1 && Dst->pred_size() > 1;
  };

  bool Changed = true;
  while (Changed) {
    Changed = false;
    SmallVector<std::pair<MachineBasicBlock *, MachineBasicBlock *>, 4> ToSplit;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineBasicBlock *Succ : MBB.successors()) {
        if (IsCriticalEdge(&MBB, Succ)) {
          ToSplit.push_back({&MBB, Succ});
          VERBOSE_DEBUG(dbgs() << "Critical edge detected. Split.\n");
        }
      }
    }

    for (std::pair<MachineBasicBlock *, MachineBasicBlock *> Split : ToSplit)
      if (Split.first->SplitCriticalEdge(Split.second, *this))
        Changed = true;
  }
  return false;
}

void ShrinkWrap2::markUsesOutsideLoops(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Keep track of the registers to attach to a basic block.
  SparseBBRegSetMap ToInsert;
  for (auto &KV : UsedCSR) {
    unsigned MBBNum = KV.first;
    const RegSet &Regs = KV.second;

    auto Mark = [&](const MachineBasicBlock *Block) {
      unsigned BlockNum = Block->getNumber();
      if (ToInsert[BlockNum].empty())
        ToInsert[BlockNum].resize(TRI.getNumRegs());
      ToInsert[BlockNum] |= Regs;
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
    UsedCSR[blockNumber(KV.first)] |= KV.second;
}

bool ShrinkWrap2::runOnMachineFunction(MachineFunction &MF) {
  // Initialize the RemarkEmitter here, because we want to emit remarks on why
  // we skipped the function.
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();
  if (skipFunction(*MF.getFunction()) || MF.empty() || !isShrinkWrapEnabled(MF))
    return false;

  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  if (ViewCFGDebug == cl::BOU_TRUE)
    MF.viewCFGOnly();

  // Initialize the dependencies.
  init(MF);

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

  // Determine all the used callee saved registers. If there are none, avoid
  // running this pass.
  determineCalleeSaves(MF);
  DEBUG(dumpUsedCSR(MF));

  // Measure from here ->
  {Measure<decltype(*this)> Measure(*this, MF);

  if (UsedCSR.empty()) {
    clear();
    return false;
  }

  // Don't bother saving if we know we're never going to return.
  removeUsesOnNoReturnPaths(MF);
  DEBUG(dbgs() << "**** After removing uses on no-return paths\n";);
  DEBUG(dumpUsedCSR(MF));

  // Collect all the CSRs. We do this now because we want to avoid iterating
  // on registers that have been removed because of no-return blocks.
  // FIXME: ShrinkWrap2: Is this really necessary?
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  Regs.resize(TRI.getNumRegs());
  for (auto &KV : UsedCSR)
    Regs |= KV.second;

  markUsesOutsideLoops(MF);
  DEBUG(dbgs() << "**** After marking uses inside loops\n";);
  DEBUG(dumpUsedCSR(MF));

  // FIXME: ShrinkWrap2: Find a better way to avoid treating added CSRs the same
  // as original ones. This is needed for postProcessResults.
  // FIXME: Probably just save / restore once per block if there is only one
  // register from the beginning.
  auto OldUsedCSR = UsedCSR;

  // Use this scope to get rid of the dataflow attributes after the
  // computation.
  {
    // Compute the dataflow attributes described by Fred C. Chow.
    AttributeMap Attrs;
    populateAttributes(MF, Attrs);
    // For each register, compute the data flow attributes.
    for (int Reg : Regs.bit_set()) {
      // FIXME: ShrinkWrap2: Avoid recomputing all the saves / restores.
      do {
        for (auto *Map : {&Saves, &Restores}) {
          for (auto &KV : *Map) {
            RegSet &Regs = KV.second;
            Regs.reset(Reg);
          }
        }
        // Compute the attributes.
        computeAttributes(Reg, MF, Attrs);
        // Gather the results.
      } while (!gatherAttributesResults(Reg, MF, Attrs));
      VERBOSE_DEBUG(dumpResults(MF));
    }
  }

  DEBUG(dbgs() << "**** Analysis results\n";);
  DEBUG(dumpResults(MF));

  postProcessResults(MF, OldUsedCSR);

  DEBUG(dbgs() << "**** Shrink-wrapping results\n");
  DEBUG(dumpResults(MF));

// FIXME: ShrinkWrap2: Remove NDEBUG.
#if !defined(NDEBUG) || defined(EXPENSIVE_CHECKS)
    verifySavesRestores(MF);
#endif // EXPENSIVE_CHECKS

  // FIXME: ShrinkWrap2: Should merge the behaviour in PEI?
  // If there is only one save block, and it's the first one, don't forward
  // anything to the MachineFrameInfo.
  // We also could have no saves, since on no-return paths, we can remove
  // saves.
  if (Saves.size() == 1 &&
      Saves.begin()->first == static_cast<unsigned>(MF.front().getNumber()) &&
      Saves.begin()->second == Regs)
    DEBUG(dbgs() << "No shrink-wrapping results.\n");
  else
    returnToMachineFrame(MF);
  } // to here, because we still need the attributes.

  clear();

  return false;
}
