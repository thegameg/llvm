//===- ShrinkWrap.cpp - Compute safe point for prolog/epilog insertion ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass takes care of adding the correct dependencies, running the
// shrink-wrapping algorithm and update MachineFrameInfo with the results if
// they are available.
//
//===----------------------------------------------------------------------===//

#include "CSRFIShrinkWrapInfo.h"
#include "DominatorShrinkWrapper.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

static cl::opt<cl::boolOrDefault>
    EnableShrinkWrapOpt("enable-shrink-wrap", cl::Hidden,
                        cl::desc("enable the shrink-wrapping pass"));

namespace {

class ShrinkWrap : public MachineFunctionPass {
  /// \brief Check if shrink wrapping is enabled for this target and function.
  static bool isShrinkWrapEnabled(const MachineFunction &MF);

public:
  static char ID;

  ShrinkWrap() : MachineFunctionPass(ID) {
    initializeShrinkWrapPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "Shrink Wrapping analysis"; }

  /// \brief Perform the shrink-wrapping analysis and update
  /// the MachineFrameInfo attached to \p MF with the results.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char ShrinkWrap::ID = 0;

char &llvm::ShrinkWrapID = ShrinkWrap::ID;

INITIALIZE_PASS_BEGIN(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)

bool ShrinkWrap::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()) || MF.empty() || !isShrinkWrapEnabled(MF))
    return false;

  // The algorithm is based on (post-)dominator trees.
  auto *MDT = &getAnalysis<MachineDominatorTree>();
  auto *MPDT = &getAnalysis<MachinePostDominatorTree>();
  // Use the information of the basic block frequency to check the
  // profitability of the new points.
  auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  // Hold the loop information. Used to determine if Save and Restore
  // are in the same loop.
  auto *MLI = &getAnalysis<MachineLoopInfo>();

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  std::unique_ptr<RegScavenger> RS(
      TRI->requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr);

  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  // Gather all the basic blocks that use CSRs and/or the stack.
  CSRFIShrinkWrapInfo SWI(MF, RS.get());
  // Run the shrink-wrapping algorithm.
  DominatorShrinkWrapper SW(MF, SWI, *MDT, *MPDT, *MBFI, *MLI);
  const BitVector &TrackedElts = SW.getTrackedElts();
  if (!TrackedElts.test(CSRFIShrinkWrapInfo::CSRFIUsedBit))
    return false;

  // If the CSRFIUsedBit is set, we can now assume that:
  // * All used elements are tracked (only one in this case)
  // * There is *exactly one* save
  // * There is *exactly one* restore
  assert(TrackedElts == SWI.getAllUsedElts() && SW.getSaves().size() == 1 &&
         SW.getSaves().size() == 1);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned SaveMBBNum = SW.getSaves().begin()->first;

  MachineBasicBlock *SaveBlock = MF.getBlockNumbered(SaveMBBNum);
  MFI.setSavePoint(SaveBlock);

  unsigned RestoreMBBNum = SW.getRestores().begin()->first;
  MachineBasicBlock *RestoreBlock = MF.getBlockNumbered(RestoreMBBNum);
  MFI.setRestorePoint(RestoreBlock);

  return false;
}

bool ShrinkWrap::isShrinkWrapEnabled(const MachineFunction &MF) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrapOpt) {
  case cl::BOU_UNSET:
    return TFI->enableShrinkWrapping(MF) &&
           // Windows with CFI has some limitations that make it impossible
           // to use shrink-wrapping.
           !MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
           // Sanitizers look at the value of the stack at the location
           // of the crash. Since a crash can happen anywhere, the
           // frame must be lowered before anything else happen for the
           // sanitizers to be able to get a correct stack frame.
           !(MF.getFunction().hasFnAttribute(Attribute::SanitizeAddress) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeThread) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeMemory) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeHWAddress));
  // If EnableShrinkWrap is set, it takes precedence on whatever the
  // target sets. The rational is that we assume we want to test
  // something related to shrink-wrapping.
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}
