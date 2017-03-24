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
// Penalty at Procedure Calls - Fred C. Chow" [1], and improved in "Post
// Register Allocation Spill Code Optimization - Christopher Lupo, Kent D.
// Wilken" [2]. The aim of this improvement is to remove the restriction that
// the current shrink-wrapping pass is having, which is having only one save /
// restore point.
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
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <set>

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"

#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"

// FIXME: ShrinkWrap2: Fix name.
#define DEBUG_TYPE "shrink-wrap2"

#define VERBOSE_DEBUG(X)                                                       \
  do {                                                                         \
    if (VerboseDebug)                                                          \
      DEBUG(X);                                                                \
  } while (0);

// FIXME: ShrinkWrap2: Add iterators to BitVector?
#define FOREACH_BIT(Var, BitVector)                                            \
  for (int Var = (BitVector).find_first(); Var >= 0;                           \
       Var = (BitVector).find_next(Var))

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
// FIXME: ShrinkWrap2: Fix name.
class ShrinkWrap2 : public MachineFunctionPass {
  using BBSet = BitVector;
  using RegSet = BitVector;
  // Idx = MBB.getNumber()
  using BBRegSetMap = SmallVector<RegSet, 8>;
  using SparseBBRegSetMap = DenseMap<unsigned, RegSet>;

  /// Store callee-saved-altering basic blocks.
  SparseBBRegSetMap UsedCSR;

  /// All the CSR used in the function. This is used for quicker iteration over
  /// the registers.
  /// FIXME: ShrinkWrap2: PEI needs this, instead of calling
  /// TII->determineCalleeSaves.
  RegSet Regs;
  RegSet TargetAddedRegs;
  RegSet TargetRemovedRegs;

  /// The dataflow attributes needed to compute shrink-wrapping locations.
  struct DataflowAttributes {
    /// The MachineFunction we're analysing.
    MachineFunction &MF;

    // FIXME: ShrinkWrap2: Explain anticipated / available and how the
    // properties are used.

    const SparseBBRegSetMap &APP;
    /// Is the register anticipated at the end of this basic block?
    BBRegSetMap ANTOUT;
    /// Is the register anticipated at the beginning of this basic block?
    BBRegSetMap ANTIN;
    /// Is the register available at the beginning of this basic block?
    BBRegSetMap AVIN;
    /// Is the register available at the end of this basic block?
    BBRegSetMap AVOUT;

    DataflowAttributes(MachineFunction &TheFunction,
                       const SparseBBRegSetMap &Used)
        : MF{TheFunction}, APP{Used} {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      for (BBRegSetMap *Map : {&ANTOUT, &ANTIN, &AVIN, &AVOUT}) {
        Map->resize(MF.getNumBlockIDs());
        for (RegSet &Regs : *Map)
          Regs.resize(TRI.getNumRegs());
      }
    }

    /// Populate the attribute maps with trivial properties from the used
    /// registers.
    void populate();
    /// Compute the attributes for one register.
    // FIXME: ShrinkWrap2: Don't do this per register.
    void compute(unsigned Reg);
    /// Save the results for this particular register.
    // FIXME: ShrinkWrap2: Don't do this per register.
    void results(unsigned Reg, SparseBBRegSetMap &Saves,
                 SparseBBRegSetMap &Restores);
    /// Dump the contents of the attributes.
    // FIXME: ShrinkWrap2: Don't do this per register.
    void dump(unsigned Reg) const;
  };

  /// Final results.
  SparseBBRegSetMap Saves;
  SparseBBRegSetMap Restores;

  /// Detect loops to avoid placing saves / restores in a loop.
  MachineLoopInfo *MLI;

  /// Emit remarks.
  MachineOptimizationRemarkEmitter *ORE;

  /// Does the function satisfy the requirements for shrink-wrapping?
  bool shouldShrinkWrap(const MachineFunction &MF);

  /// Determine all the calee saved register used in this function.
  /// This fills out the Regs set, containing all the CSR used in the entire
  /// function, and fills the UsedCSR map, containing all the CSR used per
  /// basic block.
  /// We don't use the TargetFrameLowering::determineCalleeSaves function
  /// because we need to associate each usage of a CSR to the corresponding
  /// basic block.
  // FIXME: ShrinkWrap2: Target hook might add other callee saves.
  // FIXME: ShrinkWrap2: Should we add the registers added by the target in the
  // entry / exit block(s) ?
  void determineCalleeSaves(MachineFunction &MF);
  void removeUsesOnNoReturnPaths(MachineFunction &MF);
  void dumpUsedCSR(const MachineFunction &MF) const;

  /// This algorithm relies on the fact that there are no critical edges.
  // FIXME: ShrinkWrap2: Get rid of this.
  bool splitCriticalEdges(MachineFunction &MF);

  /// Mark all the basic blocks related to a loop (inside, entry, exit) as used,
  /// if there is an usage of a CSR inside a loop. We want to avoid any save /
  /// restore operations in a loop.
  // FIXME: ShrinkWrap2: Should this be updated with the cost model?
  void markUsesInsideLoops(MachineFunction &MF);

  /// * Verify if the results are better than obvious results, like:
  ///   * CSR used in a single MBB: save at MBB.entry, restore at MBB.exit.
  /// * Remove empty entries from the Saves / Restores maps.
  // FIXME: ShrinkWrap2: This shouldn't happen, we better fix the algorithm
  // first.
  void postProcessResults(MachineFunction &MF);

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
  void init(MachineFunction &MF) { MLI = &getAnalysis<MachineLoopInfo>(); }

  /// Clear the function's state.
  void clear() {
    UsedCSR.clear();

    Regs.clear();
    TargetAddedRegs.clear();
    TargetRemovedRegs.clear();

    Saves.clear();
    Restores.clear();
  }

  // FIXME: ShrinkWrap2: releaseMemory?

public:
  static char ID;

  ShrinkWrap2() : MachineFunctionPass(ID) {
    initializeShrinkWrap2Pass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineLoopInfo>();
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
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
// FIXME: ShrinkWrap2: Fix name.
INITIALIZE_PASS_END(ShrinkWrap2, "shrink-wrap2", "Shrink Wrap Pass", false,
                    false)

void ShrinkWrap2::DataflowAttributes::populate() {
  for (auto &KV : APP) {
    unsigned MBBNum = KV.first;
    const RegSet &Regs = KV.second;
    // Setting APP also affects ANTIN and AVOUT.
    // ANTIN = APP || ANTOUT
    ANTIN[MBBNum] |= Regs;
    // AVOUT = APP || AVIN
    AVOUT[MBBNum] |= Regs;
  }
}

void ShrinkWrap2::DataflowAttributes::compute(unsigned Reg) {
  // FIXME: ShrinkWrap2: Reverse DF for ANT? DF for AV?
  // FIXME: ShrinkWrap2: Use a work list, don't compute attributes that we're
  // sure will never change.
  bool Changed = true;
  auto AssignIfDifferent = [&](RegSet &Regs, bool New) {
    if (Regs.test(Reg) != New) {
      Regs.flip(Reg);
      Changed = true;
    }
  };
  auto UsesReg = [&](unsigned MBBNum) {
    auto Found = APP.find(MBBNum);
    if (Found == APP.end())
      return false;
    return Found->second.test(Reg);
  };

  while (Changed) {
    Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      unsigned MBBNum = MBB.getNumber();
      // If there is an use of this register on *all* the paths starting from
      // this basic block, the register is anticipated at the end of this block
      // (propagate the IN attribute of successors to possibly merge saves)
      //           -
      //          | *false*             if no successor.
      // ANTOUT = |
      //          | && ANTIN(succ[i])   otherwise.
      //           -
      RegSet &ANTOUTb = ANTOUT[MBBNum];
      if (MBB.succ_empty())
        AssignIfDifferent(ANTOUTb, false);
      else {
        bool A = all_of(MBB.successors(), [&](MachineBasicBlock *S) {
          if (S == &MBB) // Ignore self.
            return true;
          return ANTIN[S->getNumber()].test(Reg);
        });
        AssignIfDifferent(ANTOUTb, A);
      }

      // If the register is used in the block, or if it is anticipated in all
      // successors it is also anticipated at the beginning, since we consider
      // entire blocks.
      //          -
      // ANTIN = | APP || ANTOUT
      //          -
      RegSet &ANTINb = ANTIN[MBBNum];
      bool NewANTIN = UsesReg(MBBNum) || ANTOUT[MBBNum].test(Reg);
      AssignIfDifferent(ANTINb, NewANTIN);

      // If there is an use of this register on *all* the paths arriving in this
      // block, then the register is available in this block (propagate the out
      // attribute of predecessors to possibly merge restores).
      //         -
      //        | *false*             if no predecessor.
      // AVIN = |
      //        | && AVOUT(pred[i])   otherwise.
      //         -
      RegSet &AVINb = AVIN[MBBNum];
      if (MBB.pred_empty())
        AssignIfDifferent(AVINb, false);
      else {
        bool A = all_of(MBB.predecessors(), [&](MachineBasicBlock *P) {
          if (P == &MBB) // Ignore self.
            return true;
          return AVOUT[P->getNumber()].test(Reg);
        });
        AssignIfDifferent(AVINb, A);
      }

      // If the register is used in the block, or if it is always available in
      // all predecessors , it is also available on exit, since we consider
      // entire blocks.
      //          -
      // AVOUT = | APP || AVIN
      //          -
      RegSet &AVOUTb = AVOUT[MBBNum];
      bool NewAVOUT = UsesReg(MBBNum) || AVIN[MBBNum].test(Reg);
      AssignIfDifferent(AVOUTb, NewAVOUT);
    }

    VERBOSE_DEBUG(dump(Reg));
  }
}

void ShrinkWrap2::DataflowAttributes::results(unsigned Reg,
                                              SparseBBRegSetMap &Saves,
                                              SparseBBRegSetMap &Restores) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    unsigned MBBNum = MBB.getNumber();
    // If the register uses are anticipated on *all* the paths leaving this
    // block, and if the register is not available at the entrance of this block
    // (if it is, then it means it has been saved already, but not restored),
    // and if *none* of the predecessors anticipates this register on their
    // output (we want to get the "highest" block), then we can identify a save
    // point for the function.
    //
    // SAVE = ANTIN && !AVIN && !ANTIN(pred[i])
    //
    bool NS = none_of(MBB.predecessors(), [&](MachineBasicBlock *P) {
      if (P == &MBB) // Ignore self.
        return false;
      return ANTIN[P->getNumber()].test(Reg);
    });
    if (ANTIN[MBBNum].test(Reg) && !AVIN[MBBNum].test(Reg) && NS) {
      RegSet &Save = Saves[MBBNum];
      if (Save.empty())
        Save.resize(TRI.getNumRegs());
      Save.set(Reg);
    }

    // If the register uses are available on *all* the paths leading to this
    // block, and if the register is not anticipated at the exit of this block
    // (if it is, then it means it has been restored already), and if *none* of
    // the successors make the register available (we want to cover the deepest
    // use), then we can identify a restrore point for the function.
    //
    // RESTORE = AVOUT && !ANTOUT && !AVOUT(succ[i])
    //
    bool NR = none_of(MBB.successors(), [&](MachineBasicBlock *S) {
      if (S == &MBB) // Ignore self.
        return false;
      return AVOUT[S->getNumber()].test(Reg);
    });
    if (AVOUT[MBBNum].test(Reg) && !ANTOUT[MBBNum].test(Reg) && NR) {
      RegSet &Restore = Restores[MBBNum];
      if (Restore.empty())
        Restore.resize(TRI.getNumRegs());
      Restore.set(Reg);
    }
  }
}

void ShrinkWrap2::DataflowAttributes::dump(unsigned Reg) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  for (MachineBasicBlock &MBB : MF) {
    unsigned MBBNum = MBB.getNumber();
    dbgs() << "BB#" << MBBNum << "<" << PrintReg(Reg, &TRI) << ">"
           << ":\n\tANTOUT : " << ANTOUT[MBBNum].test(Reg) << '\n'
           << "\tANTIN : " << ANTIN[MBBNum].test(Reg) << '\n'
           << "\tAVIN : " << AVIN[MBBNum].test(Reg) << '\n'
           << "\tAVOUT : " << AVOUT[MBBNum].test(Reg) << '\n';
  }
}

bool ShrinkWrap2::shouldShrinkWrap(const MachineFunction &MF) {
  auto BecauseOf = [&](const MachineBasicBlock *MBB, const char *Title,
                       DebugLoc Loc = {}) {
    MachineOptimizationRemarkMissed R{"shrink-wrap", Title, Loc, MBB};
    ORE->emit(R);
    return false;
  };

  // FIXME: ShrinkWrap2: Make a list of what could go wrong with exceptions.
  if (MF.empty() || MF.hasEHFunclets() || MF.callsUnwindInit() ||
      MF.callsEHReturn() /*|| MF.getFunction()->needsUnwindTableEntry()*/)
    return BecauseOf(&MF.front(), "SkippedExceptions");

  for (const MachineBasicBlock &MBB : MF) {
    // FIXME: ShrinkWrap2: Make a list of what could go wrong with exceptions.
    if (MBB.isEHPad() || MBB.isEHFuncletEntry())
      return BecauseOf(&MBB, "SkippedExceptions", MBB.front().getDebugLoc());

    // FIXME: ShrinkWrap2: This should we removed when we remove critical edge
    // splitting.
    // No indirect branches. This is required in order to be able to split
    // critical edges.
    for (const MachineInstr &MI : MBB)
      if (MI.isIndirectBranch())
        return BecauseOf(&MBB, "SkippedIndirectBranches", MI.getDebugLoc());
  }
  return true;
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

  RegSet Regs{TRI.getNumRegs()};

  // Test all the target's callee saved registers.
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];

    // Check for regmasks.
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isRegMask() && MO.clobbersPhysReg(Reg)) {
            RegSet &Used = UsedCSR[MBB.getNumber()];
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
        RegSet &Used = UsedCSR[MOI->getParent()->getParent()->getNumber()];
        // Resize if it's the first time used.
        if (Used.empty())
          Used.resize(TRI.getNumRegs());
        Used.set(Reg);
        Regs.set(Reg);
      }
      // Look for any live-ins in basic blocks.
      for (const MachineBasicBlock &MBB : MF) {
        if (MBB.isLiveIn(*AliasReg)) {
          RegSet &Used = UsedCSR[MBB.getNumber()];
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
  BitVector SavedRegsTarget;
  TFI->determineCalleeSaves(MF, SavedRegsTarget, RS.get());
  TargetAddedRegs = Regs;
  TargetAddedRegs.flip();
  TargetAddedRegs &= SavedRegsTarget;
  TargetRemovedRegs = SavedRegsTarget;
  TargetRemovedRegs.flip();
  TargetRemovedRegs &= Regs;
  TargetRemovedRegs.flip();
}

void ShrinkWrap2::removeUsesOnNoReturnPaths(MachineFunction &MF) {
  BBSet ToRemove{MF.getNumBlockIDs()};
  for (auto &KV : UsedCSR) {
    const MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
    if (![&] {
          for (const MachineBasicBlock *Block : depth_first(MBB))
            if (Block->isReturnBlock())
              return true;
          return false;
        }())
      ToRemove.set(MBB->getNumber());
  }
  FOREACH_BIT(MBBNum, ToRemove) {
    DEBUG(dbgs() << "Remove uses from no-return BB#" << MBBNum << '\n');
    UsedCSR.erase(MBBNum);
  }
}

void ShrinkWrap2::dumpUsedCSR(const MachineFunction &MF) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  BBRegSetMap Sorted{MF.getNumBlockIDs()};
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

void ShrinkWrap2::postProcessResults(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  // If there is only one use of the register, and multiple saves / restores,
  // remove them and place the save / restore at the used MBB.
  FOREACH_BIT(Reg, Regs) {
    // FIXME: ShrinkWrap2: 2x std::find_if.
    unsigned Uses =
        count_if(UsedCSR, [&](const std::pair<unsigned, RegSet> &CSR) {
          return CSR.second.test(Reg);
        });
    if (Uses == 1) {
      // Gather all the saves.
      BBSet SavesReg{MF.getNumBlockIDs()};
      for (auto &KV : Saves) {
        unsigned MBBNum = KV.first;
        const RegSet &Regs = KV.second;
        if (Regs.test(Reg))
          SavesReg.set(MBBNum);
      }

      // Gather all the restores.
      BBSet RestoresReg{MF.getNumBlockIDs()};
      for (auto &KV : Restores) {
        unsigned MBBNum = KV.first;
        const RegSet &Regs = KV.second;
        if (Regs.test(Reg))
          RestoresReg.set(MBBNum);
      }

      if (SavesReg.count() == 1 && RestoresReg.count() == 1)
        continue;

      FOREACH_BIT(MBBNum, SavesReg) { Saves[MBBNum].reset(Reg); }
      FOREACH_BIT(MBBNum, RestoresReg) { Restores[MBBNum].reset(Reg); }

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

      FOREACH_BIT(Reg, Regs) { CSI.emplace_back(Reg); }
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
  FOREACH_BIT(Reg, Regs) {
    assert(SavesReg(Reg) || RestoresReg(Reg) && "Used CSR is never saved!");
  }

  // Check that there are no saves / restores in a loop.
  for (const SparseBBRegSetMap *Map : {&Saves, &Restores}) {
    for (auto &KV : *Map) {
      MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
      assert(!MLI->getLoopFor(MBB) && "Save / restore in a loop.");
    }
  }

  // Keep track of the currently saved registers.
  RegSet Saved{TRI.getNumRegs()};
  // Cache the state of each call, to avoid redundant checks.
  std::vector<SmallVector<RegSet, 2>> Cache{MF.getNumBlockIDs()};

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
    } else {
      State.push_back(Saved);
    }

    VERBOSE_DEBUG(
        dbgs() << "IN: BB#" << MBBNum << ": "; FOREACH_BIT(Reg, Saved) {
          dbgs() << PrintReg(Reg, &TRI) << " ";
        } dbgs() << '\n');

    const RegSet &SavesMBB = Saves.lookup(MBBNum);
    const RegSet &RestoresMBB = Restores.lookup(MBBNum);

    // Get the intersection of the currently saved registers and the
    // registers to be saved for this basic block. If the intersection is
    // not empty, it means we have nested saves for the same register.
    RegSet Intersection{SavesMBB};
    Intersection &= Saved;

    FOREACH_BIT(Reg, Intersection) {
      DEBUG(dbgs() << PrintReg(Reg, &TRI) << " is saved twice.\n");
    }

    assert(Intersection.count() == 0 && "Nested saves for the same register.");
    Intersection.reset();

    // Save the registers to be saved.
    FOREACH_BIT(Reg, SavesMBB) {
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
    FOREACH_BIT(Reg, RestoresMBB) {
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
    FOREACH_BIT(Reg, RestoresMBB) {
      Saved.set(Reg);
      VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Save "
                           << PrintReg(Reg, &TRI) << ".\n");
    }
    FOREACH_BIT(Reg, SavesMBB) {
      Saved.reset(Reg);
      VERBOSE_DEBUG(dbgs() << "OUT: BB#" << MBBNum << ": Restore "
                           << PrintReg(Reg, &TRI) << ".\n");
    }
  };

  verifySavesRestoresRec(&MF.front());
}

void ShrinkWrap2::dumpResults(MachineFunction &MF) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // for (MachineBasicBlock &MBB : MF) {
  for (unsigned MBBNum = 0; MBBNum < MF.getNumBlockIDs(); ++MBBNum) {
    if (Saves.count(MBBNum) || Restores.count(MBBNum)) {
      DEBUG(dbgs() << "BB#" << MBBNum << ": Saves: ");
      FOREACH_BIT(Reg, Saves.lookup(MBBNum)) {
        DEBUG(dbgs() << PrintReg(Reg, &TRI) << ", ");
      }
      DEBUG(dbgs() << "| Restores: ");
      FOREACH_BIT(Reg, Restores.lookup(MBBNum)) {
        DEBUG(dbgs() << PrintReg(Reg, &TRI) << ", ");
      }

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

void ShrinkWrap2::markUsesInsideLoops(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Keep track of the registers to attach to a basic block.
  SparseBBRegSetMap ToInsert;
  for (auto &KV : UsedCSR) {
    unsigned MBBNum = KV.first;
    const MachineBasicBlock *MBB = MF.getBlockNumbered(MBBNum);
    RegSet &Regs = KV.second;

    if (MachineLoop *Loop = MLI->getLoopFor(MBB)) {
      DEBUG(dbgs() << "Loop for CSR: BB#" << MBBNum << '\n');

      // Get the most top loop.
      while (Loop->getParentLoop())
        Loop = Loop->getParentLoop();

      VERBOSE_DEBUG(Loop->dump());

      auto Mark = [&](MachineBasicBlock *Block) {
        unsigned BlockNum = Block->getNumber();
        if (ToInsert[BlockNum].empty())
          ToInsert[BlockNum].resize(TRI.getNumRegs());
        ToInsert[BlockNum] |= Regs;
        VERBOSE_DEBUG(dbgs() << "Mark: BB#" << BlockNum << '\n');
      };

      // Mark all around the top block.
      MachineBasicBlock *Top = Loop->getTopBlock();
      for (auto &Around : {Top->successors(), Top->predecessors()})
        for (MachineBasicBlock *Block : Around)
          Mark(Block);

      // Mark all the blocks of the loop.
      for (MachineBasicBlock *Block : Loop->getBlocks())
        Mark(Block);

      // Mark all the exit blocks of the loop.
      SmallVector<MachineBasicBlock *, 4> ExitBlocks;
      Loop->getExitBlocks(ExitBlocks);
      for (MachineBasicBlock *Exit : ExitBlocks)
        Mark(Exit);
    }
  }

  for (auto &KV : ToInsert)
    UsedCSR[KV.first] |= KV.second;
}

bool ShrinkWrap2::runOnMachineFunction(MachineFunction &MF) {
  // Initialize the RemarkEmitter here, because we want to emit remarks on why
  // we skipped the function.
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();

  if (skipFunction(*MF.getFunction()) || MF.empty() ||
      EnableShrinkWrap2Opt == cl::BOU_FALSE || !shouldShrinkWrap(MF))
    return false;

  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  if (ViewCFGDebug == cl::BOU_TRUE)
    VERBOSE_DEBUG(MF.viewCFGOnly());

  // FIXME: ShrinkWrap2: Sadly, I we have to split critical edges before looking
  // for all the used CSRs, since liveness analysis is impacted. This means that
  // even for programs with no CSRs used, we have to split all the critical
  // edges.
  splitCriticalEdges(MF);

  // Initialize the dependencies.
  init(MF);

  // Determine all the used callee saved registers. If there are none, avoid
  // running this pass.
  determineCalleeSaves(MF);
  DEBUG(dumpUsedCSR(MF));

  if (ViewCFGDebug == cl::BOU_TRUE)
    DEBUG(MF.viewCFGOnly());

  if (UsedCSR.empty()) {
    clear();
    return false;
  }

  // Don't bother saving if we know we're never going to return.
  removeUsesOnNoReturnPaths(MF);
  DEBUG(dbgs() << "**** After removing uses on no-return paths\n";);
  DEBUG(dumpUsedCSR(MF));

  // Collect all the CSRs. We do this now because we want to avoid iterating
  // on no-return blocks.
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  Regs.resize(TRI.getNumRegs());
  for (auto &KV : UsedCSR)
    Regs |= KV.second;

  markUsesInsideLoops(MF);
  DEBUG(dbgs() << "**** After marking uses inside loops\n";);
  DEBUG(dumpUsedCSR(MF));

  // Use this scope to get rid of the dataflow attributes after the
  // computation.
  {
    // Compute the dataflow attributes described by Fred C. Chow.
    DataflowAttributes Attr{MF, UsedCSR};
    Attr.populate();
    // For each register, compute the data flow attributes.
    FOREACH_BIT(Reg, Regs) {
      // FIXME: ShrinkWrap2: Avoid recomputing all the saves / restores.
      for (auto *Map : {&Saves, &Restores}) {
        for (auto &KV : *Map) {
          RegSet &Regs = KV.second;
          Regs.reset(Reg);
        }
      }
      // Compute the attributes.
      Attr.compute(Reg);
      // Gather the results.
      Attr.results(Reg, Saves, Restores);
      VERBOSE_DEBUG(dumpResults(MF));
    }
  }

  DEBUG(dbgs() << "**** Analysis results\n";);
  DEBUG(dumpResults(MF));

  postProcessResults(MF);

  DEBUG(dbgs() << "**** Shrink-wrapping results\n");
  DEBUG(dumpResults(MF));

  // FIXME: ShrinkWrap2: Enable EXPENSIVE_CHECKS.
  //#ifdef EXPENSIVE_CHECKS
  verifySavesRestores(MF);
  //#endif // EXPENSIVE_CHECKS

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
  clear();

  return false;
}
