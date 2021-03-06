//===-- PrologEpilogInserter.cpp - Insert Prolog/Epilog code in function --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is responsible for finalizing the functions frame layout, saving
// callee saved registers, and for emitting prolog & epilog code for the
// function.
//
// This pass must be run after register allocation.  After this pass is
// executed, it is illegal to construct MO_FrameIndex operands.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/ShrinkWrapper.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCAsmInfo.h"
#include <climits>

using namespace llvm;

#define DEBUG_TYPE "prologepilog"

// FIXME: ShrinkWrap2: Fix name.
cl::opt<cl::boolOrDefault>
    EnableShrinkWrap2Opt("enable-shrink-wrap2", cl::Hidden,
                         cl::desc("enable the shrink-wrapping 2 pass"));

typedef SmallVector<MachineBasicBlock *, 4> MBBVector;

namespace {
class PEI : public MachineFunctionPass {
public:
  static char ID;
  PEI() : MachineFunctionPass(ID) {
    initializePEIPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// \brief Check if shrink wrapping is enabled for this target and function.
  bool isShrinkWrapEnabled(const MachineFunction &MF);

  MachineFunctionProperties getRequiredProperties() const override {
    MachineFunctionProperties MFP;
    if (UsesCalleeSaves)
      MFP.set(MachineFunctionProperties::Property::NoVRegs);
    return MFP;
  }

  /// runOnMachineFunction - Insert prolog/epilog code and replace abstract
  /// frame indexes with appropriate references.
  ///
  bool runOnMachineFunction(MachineFunction &Fn) override;

private:
  std::function<void(MachineFunction &MF)> SpillCalleeSavedRegisters;
  std::function<void(MachineFunction &MF, RegScavenger &RS)>
      ScavengeFrameVirtualRegs;

  bool UsesCalleeSaves = false;

  RegScavenger *RS;

  // MinCSFrameIndex, MaxCSFrameIndex - Keeps the range of callee saved
  // stack frame indexes.
  unsigned MinCSFrameIndex = std::numeric_limits<unsigned>::max();
  unsigned MaxCSFrameIndex = 0;

  // FIXME: ShrinkWrap2: Merge the shrink-wrapping logic here.
  // Save and Restore blocks of the current function. Typically there is a
  // single save block, unless Windows EH funclets are involved.
  MBBVector SaveBlocks;
  MBBVector RestoreBlocks;

  // Flag to control whether to use the register scavenger to resolve
  // frame index materialization registers. Set according to
  // TRI->requiresFrameIndexScavenging() for the current function.
  bool FrameIndexVirtualScavenging;

  // Flag to control whether the scavenger should be passed even though
  // FrameIndexVirtualScavenging is used.
  bool FrameIndexEliminationScavenging;

  // Emit remarks.
  MachineOptimizationRemarkEmitter *ORE = nullptr;


  void calculateCallFrameInfo(MachineFunction &Fn);
  void calculateSaveRestoreBlocks(MachineFunction &Fn);
  void doSpillCalleeSavedRegs(MachineFunction &MF);
  void doSpillCalleeSavedRegsShrinkWrap2(MachineFunction &Fn,
                                         CalleeSavedMap &Saves,
                                         CalleeSavedMap &Restores);
  void calculateFrameObjectOffsets(MachineFunction &Fn);
  void replaceFrameIndices(MachineFunction &Fn);
  void replaceFrameIndices(MachineBasicBlock *BB, MachineFunction &Fn,
                           int &SPAdj);
  void insertPrologEpilogCode(MachineFunction &Fn);
};
} // namespace

char PEI::ID = 0;
char &llvm::PrologEpilogCodeInserterID = PEI::ID;

static cl::opt<unsigned>
WarnStackSize("warn-stack-size", cl::Hidden, cl::init((unsigned)-1),
              cl::desc("Warn for stack size bigger than the given"
                       " number"));

INITIALIZE_PASS_BEGIN(PEI, DEBUG_TYPE, "Prologue/Epilogue Insertion", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(StackProtector)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
INITIALIZE_PASS_END(PEI, DEBUG_TYPE,
                    "Prologue/Epilogue Insertion & Frame Finalization", false,
                    false)

MachineFunctionPass *llvm::createPrologEpilogInserterPass() {
  return new PEI();
}

STATISTIC(NumBytesStackSpace,
          "Number of bytes used for stack in all functions");

void PEI::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addPreserved<MachineLoopInfo>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addRequired<StackProtector>();
  AU.addRequired<MachineBlockFrequencyInfo>();
  AU.addRequired<MachineOptimizationRemarkEmitterPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool PEI::isShrinkWrapEnabled(const MachineFunction &MF) {
  auto BecauseOf = [&](const char *Title, const char *Msg, DebugLoc Loc = {}) {
    MachineOptimizationRemarkMissed R(DEBUG_TYPE, Title, Loc, &MF.front());
    R << "Couldn't shrink-wrap this function because " << Msg;
    ORE->emit(R);
    return false;
  };

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrap2Opt) {
  case cl::BOU_UNSET: {
    if (MF.getTarget().getOptLevel() == CodeGenOpt::None)
      return BecauseOf("ShrinkWrapDisabledOpt",
                       "shrink-wrapping is enabled at O1+.");

    if (!TFI->enableShrinkWrapping(MF))
      return BecauseOf("ShrinkWrapDisabledTarget",
                       "shrink-wrapping is not enabled on this target.");
    // Windows with CFI has some limitations that make it impossible
    // to use shrink-wrapping.
    if (MF.getTarget().getMCAsmInfo()->usesWindowsCFI())
      return BecauseOf("ShrinkWrapDisabledWindowsCFI",
                       "shrink-wrapping does not support Windows CFI yet.");

    // Sanitizers look at the value of the stack at the location
    // of the crash. Since a crash can happen anywhere, the
    // frame must be lowered before anything else happen for the
    // sanitizers to be able to get a correct stack frame.
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeAddress))
      return BecauseOf("ShrinkWrapDisabledASAN",
                       "shrink-wrapping can't be enabled with ASAN.");
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeThread))
      return BecauseOf("ShrinkWrapDisabledTSAN",
                       "shrink-wrapping can't be enabled with TSAN.");
    if (MF.getFunction()->hasFnAttribute(Attribute::SanitizeMemory))
      return BecauseOf("ShrinkWrapDisabledMSAN",
                       "shrink-wrapping can't be enabled with MSAN.");
  }
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}

/// StackObjSet - A set of stack object indexes
typedef SmallSetVector<int, 8> StackObjSet;

/// runOnMachineFunction - Insert prolog/epilog code and replace abstract
/// frame indexes with appropriate references.
///
bool PEI::runOnMachineFunction(MachineFunction &Fn) {
  if (!SpillCalleeSavedRegisters) {
    const TargetMachine &TM = Fn.getTarget();
    if (!TM.usesPhysRegsForPEI()) {
      SpillCalleeSavedRegisters = [](MachineFunction &) {};
      ScavengeFrameVirtualRegs = [](MachineFunction &, RegScavenger &) {};
    } else {
      SpillCalleeSavedRegisters = [this](MachineFunction &Fn) {
        this->doSpillCalleeSavedRegs(Fn);
      };
      ScavengeFrameVirtualRegs = scavengeFrameVirtualRegs;
      UsesCalleeSaves = true;
    }
  }

  const Function* F = Fn.getFunction();
  const TargetRegisterInfo *TRI = Fn.getSubtarget().getRegisterInfo();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();

  MachineFrameInfo &MFI = Fn.getFrameInfo();
  RS = TRI->requiresRegisterScavenging(Fn) ? new RegScavenger() : nullptr;
  // FIXME: ShrinkWrap2: Temporary hack. Remove.
  MFI.RS = RS;
  FrameIndexVirtualScavenging = TRI->requiresFrameIndexScavenging(Fn);
  FrameIndexEliminationScavenging = (RS && !FrameIndexVirtualScavenging) ||
    TRI->requiresFrameIndexReplacementScavenging(Fn);
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();

  // Calculate the MaxCallFrameSize and AdjustsStack variables for the
  // function's frame information. Also eliminates call frame pseudo
  // instructions.
  calculateCallFrameInfo(Fn);

  // Determine placement of CSR spill/restore code and prolog/epilog code:
  // place all spills in the entry block, all restores in return blocks.
  calculateSaveRestoreBlocks(Fn);

  // Handle CSR spilling and restoring, for targets that need it.
  SpillCalleeSavedRegisters(Fn);

  // Allow the target machine to make final modifications to the function
  // before the frame layout is finalized.
  TFI->processFunctionBeforeFrameFinalized(Fn, RS);

  // Calculate actual frame offsets for all abstract stack objects...
  calculateFrameObjectOffsets(Fn);

  // Add prolog and epilog code to the function.  This function is required
  // to align the stack frame as necessary for any stack variables or
  // called functions.  Because of this, calculateCalleeSavedRegisters()
  // must be called before this function in order to set the AdjustsStack
  // and MaxCallFrameSize variables.
  if (!F->hasFnAttribute(Attribute::Naked))
    insertPrologEpilogCode(Fn);

  // Replace all MO_FrameIndex operands with physical register references
  // and actual offsets.
  //
  replaceFrameIndices(Fn);

  // If register scavenging is needed, as we've enabled doing it as a
  // post-pass, scavenge the virtual registers that frame index elimination
  // inserted.
  if (TRI->requiresRegisterScavenging(Fn) && FrameIndexVirtualScavenging) {
      ScavengeFrameVirtualRegs(Fn, *RS);

      // Clear any vregs created by virtual scavenging.
      Fn.getRegInfo().clearVirtRegs();
  }

  // Warn on stack size when we exceeds the given limit.
  uint64_t StackSize = MFI.getStackSize();
  if (WarnStackSize.getNumOccurrences() > 0 && WarnStackSize < StackSize) {
    DiagnosticInfoStackSize DiagStackSize(*F, StackSize);
    F->getContext().diagnose(DiagStackSize);
  }

  // FIXME: ShrinkWrap2: Temporary hack. Remove.
  MFI.RS = nullptr;
  delete RS;
  SaveBlocks.clear();
  RestoreBlocks.clear();
  MFI.setSavePoint(nullptr);
  MFI.setRestorePoint(nullptr);
  return true;
}

/// Calculate the MaxCallFrameSize and AdjustsStack
/// variables for the function's frame information and eliminate call frame
/// pseudo instructions.
void PEI::calculateCallFrameInfo(MachineFunction &Fn) {
  const TargetInstrInfo &TII = *Fn.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = Fn.getFrameInfo();

  unsigned MaxCallFrameSize = 0;
  bool AdjustsStack = MFI.adjustsStack();

  // Get the function call frame set-up and tear-down instruction opcode
  unsigned FrameSetupOpcode = TII.getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();

  // Early exit for targets which have no call frame setup/destroy pseudo
  // instructions.
  if (FrameSetupOpcode == ~0u && FrameDestroyOpcode == ~0u)
    return;

  std::vector<MachineBasicBlock::iterator> FrameSDOps;
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB)
    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      if (TII.isFrameInstr(*I)) {
        unsigned Size = TII.getFrameSize(*I);
        if (Size > MaxCallFrameSize) MaxCallFrameSize = Size;
        AdjustsStack = true;
        FrameSDOps.push_back(I);
      } else if (I->isInlineAsm()) {
        // Some inline asm's need a stack frame, as indicated by operand 1.
        unsigned ExtraInfo = I->getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
        if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
          AdjustsStack = true;
      }

  assert(!MFI.isMaxCallFrameSizeComputed() ||
         (MFI.getMaxCallFrameSize() == MaxCallFrameSize &&
          MFI.adjustsStack() == AdjustsStack));
  MFI.setAdjustsStack(AdjustsStack);
  MFI.setMaxCallFrameSize(MaxCallFrameSize);

  for (std::vector<MachineBasicBlock::iterator>::iterator
         i = FrameSDOps.begin(), e = FrameSDOps.end(); i != e; ++i) {
    MachineBasicBlock::iterator I = *i;

    // If call frames are not being included as part of the stack frame, and
    // the target doesn't indicate otherwise, remove the call frame pseudos
    // here. The sub/add sp instruction pairs are still inserted, but we don't
    // need to track the SP adjustment for frame index elimination.
    if (TFI->canSimplifyCallFramePseudos(Fn))
      TFI->eliminateCallFramePseudoInstr(Fn, *I->getParent(), I);
  }
}

/// Compute the sets of entry and return blocks for saving and restoring
/// callee-saved registers, and placing prolog and epilog code.
void PEI::calculateSaveRestoreBlocks(MachineFunction &Fn) {
  const MachineFrameInfo &MFI = Fn.getFrameInfo();

  // Even when we do not change any CSR, we still want to insert the
  // prologue and epilogue of the function.
  // So set the save points for those.

  // Use the points found by shrink-wrapping, if any.
  if (MFI.getSavePoint()) {
    // FIXME: ShrinkWrap2: Remove check.
    assert(!MFI.getShouldUseShrinkWrap2() && "Mixing shrink-wrapping passes.");
    SaveBlocks.push_back(MFI.getSavePoint());
    assert(MFI.getRestorePoint() && "Both restore and save must be set");
    MachineBasicBlock *RestoreBlock = MFI.getRestorePoint();
    // If RestoreBlock does not have any successor and is not a return block
    // then the end point is unreachable and we do not need to insert any
    // epilogue.
    if (!RestoreBlock->succ_empty() || RestoreBlock->isReturnBlock())
      RestoreBlocks.push_back(RestoreBlock);
    return;
  }

  // Save refs to entry and return blocks.
  SaveBlocks.push_back(&Fn.front());
  for (MachineBasicBlock &MBB : Fn) {
    if (MBB.isEHFuncletEntry())
      SaveBlocks.push_back(&MBB);
    if (MBB.isReturnBlock())
      RestoreBlocks.push_back(&MBB);
  }
}

static void assignCalleeSavedSpillSlots(MachineFunction &F,
                                        const BitVector &SavedRegs,
                                        unsigned &MinCSFrameIndex,
                                        unsigned &MaxCSFrameIndex) {
  if (SavedRegs.empty())
    return;

  const TargetRegisterInfo *RegInfo = F.getSubtarget().getRegisterInfo();
  const MCPhysReg *CSRegs = F.getRegInfo().getCalleeSavedRegs();

  std::vector<CalleeSavedInfo> CSI;
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];
    if (SavedRegs.test(Reg))
      CSI.push_back(CalleeSavedInfo(Reg));
  }

  const TargetFrameLowering *TFI = F.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = F.getFrameInfo();
  if (!TFI->assignCalleeSavedSpillSlots(F, RegInfo, CSI)) {
    // If target doesn't implement this, use generic code.

    if (CSI.empty())
      return; // Early exit if no callee saved registers are modified!

    unsigned NumFixedSpillSlots;
    const TargetFrameLowering::SpillSlot *FixedSpillSlots =
        TFI->getCalleeSavedSpillSlots(NumFixedSpillSlots);

    // Now that we know which registers need to be saved and restored, allocate
    // stack slots for them.
    for (auto &CS : CSI) {
      unsigned Reg = CS.getReg();
      const TargetRegisterClass *RC = RegInfo->getMinimalPhysRegClass(Reg);

      int FrameIdx;
      if (RegInfo->hasReservedSpillSlot(F, Reg, FrameIdx)) {
        CS.setFrameIdx(FrameIdx);
        continue;
      }

      // Check to see if this physreg must be spilled to a particular stack slot
      // on this target.
      const TargetFrameLowering::SpillSlot *FixedSlot = FixedSpillSlots;
      while (FixedSlot != FixedSpillSlots + NumFixedSpillSlots &&
             FixedSlot->Reg != Reg)
        ++FixedSlot;

      unsigned Size = RegInfo->getSpillSize(*RC);
      if (FixedSlot == FixedSpillSlots + NumFixedSpillSlots) {
        // Nope, just spill it anywhere convenient.
        unsigned Align = RegInfo->getSpillAlignment(*RC);
        unsigned StackAlign = TFI->getStackAlignment();

        // We may not be able to satisfy the desired alignment specification of
        // the TargetRegisterClass if the stack alignment is smaller. Use the
        // min.
        Align = std::min(Align, StackAlign);
        FrameIdx = MFI.CreateStackObject(Size, Align, true);
        if ((unsigned)FrameIdx < MinCSFrameIndex) MinCSFrameIndex = FrameIdx;
        if ((unsigned)FrameIdx > MaxCSFrameIndex) MaxCSFrameIndex = FrameIdx;
      } else {
        // Spill it to the stack where we must.
        FrameIdx = MFI.CreateFixedSpillStackObject(Size, FixedSlot->Offset);
      }

      CS.setFrameIdx(FrameIdx);
    }
  }

  MFI.setCalleeSavedInfo(CSI);
  // FIXME: ShrinkWrap2: AArch64FrameLowering needs to call
  // computeCaleeSaveRegisterPairs *after* calling the generic code above. We
  // could duplicate this code inside
  // AArch64FrameLowering::assignCalleeSavedSpillSlots, but we need to update
  // MinCSFrameIndex and MaxCSFrameIndex.
  if (MFI.getShouldUseShrinkWrap2() || MFI.getShouldUseStackShrinkWrap2())
    TFI->processValidCalleeSavedInfo(F, RegInfo, CSI);
}

/// Helper function to update the liveness information for the callee-saved
/// registers.
static void updateLiveness(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  // Visited will contain all the basic blocks that are in the region
  // where the callee saved registers are alive:
  // - Anything that is not Save or Restore -> LiveThrough.
  // - Save -> LiveIn.
  // - Restore -> LiveOut.
  // The live-out is not attached to the block, so no need to keep
  // Restore in this set.
  SmallPtrSet<MachineBasicBlock *, 8> Visited;
  SmallVector<MachineBasicBlock *, 8> WorkList;
  MachineBasicBlock *Entry = &MF.front();
  MachineBasicBlock *Save = MFI.getSavePoint();

  if (!Save)
    Save = Entry;

  if (Entry != Save) {
    WorkList.push_back(Entry);
    Visited.insert(Entry);
  }
  Visited.insert(Save);

  MachineBasicBlock *Restore = MFI.getRestorePoint();
  if (Restore)
    // By construction Restore cannot be visited, otherwise it
    // means there exists a path to Restore that does not go
    // through Save.
    WorkList.push_back(Restore);

  while (!WorkList.empty()) {
    const MachineBasicBlock *CurBB = WorkList.pop_back_val();
    // By construction, the region that is after the save point is
    // dominated by the Save and post-dominated by the Restore.
    if (CurBB == Save && Save != Restore)
      continue;
    // Enqueue all the successors not already visited.
    // Those are by construction either before Save or after Restore.
    for (MachineBasicBlock *SuccBB : CurBB->successors())
      if (Visited.insert(SuccBB).second)
        WorkList.push_back(SuccBB);
  }

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    for (MachineBasicBlock *MBB : Visited) {
      MCPhysReg Reg = CSI[i].getReg();
      // Add the callee-saved register as live-in.
      // It's killed at the spill.
      if (!MRI.isReserved(Reg) && !MBB->isLiveIn(Reg))
        MBB->addLiveIn(Reg);
    }
  }
}

/// Insert restore code for the callee-saved registers used in the function.
static void insertCSRSaves(MachineBasicBlock &SaveBlock,
                           ArrayRef<CalleeSavedInfo> CSI) {
  MachineFunction &Fn = *SaveBlock.getParent();
  const TargetInstrInfo &TII = *Fn.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = Fn.getSubtarget().getRegisterInfo();

  MachineBasicBlock::iterator I = SaveBlock.begin();
  if (!TFI->spillCalleeSavedRegisters(SaveBlock, I, CSI, TRI)) {
    for (const CalleeSavedInfo &CS : CSI) {
      // Insert the spill to the stack frame.
      unsigned Reg = CS.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.storeRegToStackSlot(SaveBlock, I, Reg, true, CS.getFrameIdx(), RC,
                              TRI);
      std::prev(I)->setFlag(MachineInstr::FrameSetup);

      // FIXME: ShrinkWrap2: Check wether we need CFI, even though it is
      // ignored by the AsmPrinter.
      // Emit CFI for every CSR spill:
      // .cfi_offset %reg, off
      MachineFrameInfo &MFI = Fn.getFrameInfo();
      if (MFI.getShouldUseShrinkWrap2()) {
        unsigned Offset = MFI.getObjectOffset(CS.getFrameIdx());
        const MCRegisterInfo *MRI = Fn.getMMI().getContext().getRegisterInfo();
        unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
        unsigned CFIIndex = Fn.addFrameInst(
            MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
        BuildMI(SaveBlock, I, {}, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }

    }
  }
}

/// Insert restore code for the callee-saved registers used in the function.
static void insertCSRRestores(MachineBasicBlock &RestoreBlock,
                              std::vector<CalleeSavedInfo> &CSI) {
  MachineFunction &Fn = *RestoreBlock.getParent();
  const TargetInstrInfo &TII = *Fn.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = Fn.getSubtarget().getRegisterInfo();

  // Restore all registers immediately before the return and any
  // terminators that precede it.
  MachineBasicBlock::iterator I = RestoreBlock.getFirstTerminator();

  if (!TFI->restoreCalleeSavedRegisters(RestoreBlock, I, CSI, TRI)) {
    for (const CalleeSavedInfo &CI : reverse(CSI)) {
      unsigned Reg = CI.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.loadRegFromStackSlot(RestoreBlock, I, Reg, CI.getFrameIdx(), RC, TRI);
      std::prev(I)->setFlag(MachineInstr::FrameDestroy);
      assert(I != RestoreBlock.begin() &&
             "loadRegFromStackSlot didn't insert any code!");
      // Insert in reverse order.  loadRegFromStackSlot can insert
      // multiple instructions.
      // FIXME: ShrinkWrap2: Check wether we need CFI, even though it is
      // ignored by the AsmPrinter.
      // Emit CFI for every CSR restore.
      // .cfi_restore %reg
      MachineFrameInfo &MFI = Fn.getFrameInfo();
      if (MFI.getShouldUseShrinkWrap2()) {
        MachineModuleInfo &MMI = Fn.getMMI();
        const MCRegisterInfo *MRI = MMI.getContext().getRegisterInfo();
        unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
        unsigned CFIIndex =
            Fn.addFrameInst(MCCFIInstruction::createRestore(nullptr, DwarfReg));
        BuildMI(RestoreBlock, I, {}, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }

    }
  }
}

// FIXME: ShrinkWrap2: Name.
void PEI::doSpillCalleeSavedRegsShrinkWrap2(MachineFunction &Fn,
                                            CalleeSavedMap &Saves,
                                            CalleeSavedMap &Restores) {
  const TargetRegisterInfo &TRI = *Fn.getSubtarget().getRegisterInfo();
  MachineFrameInfo &MFI = Fn.getFrameInfo();

  // Now gather the callee-saved registers we found using shrink-wrapping.
  // FIXME: ShrinkWrap2: We already gathered all the CSRs in ShrinkWrap. Reuse
  // somehow?
  BitVector ShrinkWrapSavedRegs(TRI.getNumRegs());
  for (auto &Save : Saves)
    for (const CalleeSavedInfo &CSI : Save.second)
      ShrinkWrapSavedRegs.set(CSI.getReg());

  // FIXME: ShrinkWrap2: Re-use stack slots.
  assignCalleeSavedSpillSlots(Fn, ShrinkWrapSavedRegs, MinCSFrameIndex,
                              MaxCSFrameIndex);

  MFI.setCalleeSavedInfoValid(true);

  if (Fn.getFunction()->hasFnAttribute(Attribute::Naked))
    return;

  // FIXME: ShrinkWrap2: This is awful. We first call
  // assignCalleeSavedSpillSlots, that fills MFI.CalleeSavedInfo which is used
  // for the ENTIRE function. Then, we need to reassign the FrameIdx back to the
  // Saves / Restores map.
  const std::vector<CalleeSavedInfo> &CSIs = MFI.getCalleeSavedInfo();
  for (auto *Map : {&Saves, &Restores}) {
    for (auto &Elt : *Map) {
      for (const CalleeSavedInfo &CSI : Elt.second) {
        unsigned Reg = CSI.getReg();
        // Look for the register in the assigned CSIs, and reassign it in the
        // map.
        auto It = find_if(CSIs, [&](const CalleeSavedInfo &NewCSI) {
          return NewCSI.getReg() == Reg;
        });
        if (It != CSIs.end())
          // FIXME: ShrinkWrap2: const_cast...
          const_cast<CalleeSavedInfo &>(CSI).setFrameIdx(It->getFrameIdx());
      }
    }
  }

  for (auto &Save : Saves)
    insertCSRSaves(*Save.first, Save.second);

  for (auto &Restore : Restores)
    insertCSRRestores(*Restore.first, Restore.second);
}

void PEI::doSpillCalleeSavedRegs(MachineFunction &Fn) {
  const Function *F = Fn.getFunction();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = Fn.getFrameInfo();
  MinCSFrameIndex = std::numeric_limits<unsigned>::max();
  MaxCSFrameIndex = 0;


  /// If any, contains better save points for the prologue found by
  /// shrink-wrapping.
  CalleeSavedMap Saves;
  /// If any, contains better restore points for the epilogue found by
  /// shrink-wrapping.
  CalleeSavedMap Restores;

  if (!Fn.empty() && isShrinkWrapEnabled(Fn)) {
    ShrinkWrapper SW(Fn);
    auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
    if (SW.areResultsInteresting(MBFI)) {
      MachineFrameInfo &MFI = Fn.getFrameInfo();
      MFI.setShouldUseShrinkWrap2(true);
      SW.emitRemarks(ORE, MBFI);
    }
    auto &SWSaves = SW.getSaves();
    auto &SWRestores = SW.getRestores();
    const MCPhysReg *CSRegs = Fn.getRegInfo().getCalleeSavedRegs();
    auto Transform = [&](const DenseMap<unsigned, BitVector> &Src,
                         CalleeSavedMap &Dst) {
      for (auto &KV : Src) {
        MachineBasicBlock *MBB = Fn.getBlockNumbered(KV.first);
        const BitVector &Regs = KV.second;
        std::vector<CalleeSavedInfo> &CSI = Dst[MBB];

        for (unsigned RegIdx : Regs.set_bits())
          CSI.emplace_back(CSRegs[RegIdx]);
      }
    };
    Transform(SWSaves, Saves);
    Transform(SWRestores, Restores);
    TFI->processSaveRestorePoints(Fn, Saves, Restores);
  }

  // FIXME: ShrinkWrap2: Share code somehow.
  if (MFI.getShouldUseShrinkWrap2() || MFI.getShouldUseStackShrinkWrap2())
    return doSpillCalleeSavedRegsShrinkWrap2(Fn, Saves, Restores);

  // Determine which of the registers in the callee save list should be saved.
  BitVector SavedRegs;
  TFI->determineCalleeSaves(Fn, SavedRegs, RS);

  // Assign stack slots for any callee-saved registers that must be spilled.
  assignCalleeSavedSpillSlots(Fn, SavedRegs, MinCSFrameIndex, MaxCSFrameIndex);

  // Add the code to save and restore the callee saved registers.
  if (!F->hasFnAttribute(Attribute::Naked)) {
    MFI.setCalleeSavedInfoValid(true);

    std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    if (!CSI.empty()) {
      for (MachineBasicBlock *SaveBlock : SaveBlocks) {
        insertCSRSaves(*SaveBlock, CSI);
        // Update the live-in information of all the blocks up to the save
        // point.
        updateLiveness(Fn);
      }
      for (MachineBasicBlock *RestoreBlock : RestoreBlocks)
        insertCSRRestores(*RestoreBlock, CSI);
    }
  }
}

/// AdjustStackOffset - Helper function used to adjust the stack frame offset.
static inline void
AdjustStackOffset(MachineFrameInfo &MFI, int FrameIdx,
                  bool StackGrowsDown, int64_t &Offset,
                  unsigned &MaxAlign, unsigned Skew) {
  // If the stack grows down, add the object size to find the lowest address.
  if (StackGrowsDown)
    Offset += MFI.getObjectSize(FrameIdx);

  unsigned Align = MFI.getObjectAlignment(FrameIdx);

  // If the alignment of this object is greater than that of the stack, then
  // increase the stack alignment to match.
  MaxAlign = std::max(MaxAlign, Align);

  // Adjust to alignment boundary.
  Offset = alignTo(Offset, Align, Skew);

  if (StackGrowsDown) {
    DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << -Offset << "]\n");
    MFI.setObjectOffset(FrameIdx, -Offset); // Set the computed offset
  } else {
    DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << Offset << "]\n");
    MFI.setObjectOffset(FrameIdx, Offset);
    Offset += MFI.getObjectSize(FrameIdx);
  }
}

/// Compute which bytes of fixed and callee-save stack area are unused and keep
/// track of them in StackBytesFree.
///
static inline void
computeFreeStackSlots(MachineFrameInfo &MFI, bool StackGrowsDown,
                      unsigned MinCSFrameIndex, unsigned MaxCSFrameIndex,
                      int64_t FixedCSEnd, BitVector &StackBytesFree) {
  // Avoid undefined int64_t -> int conversion below in extreme case.
  if (FixedCSEnd > std::numeric_limits<int>::max())
    return;

  StackBytesFree.resize(FixedCSEnd, true);

  SmallVector<int, 16> AllocatedFrameSlots;
  // Add fixed objects.
  for (int i = MFI.getObjectIndexBegin(); i != 0; ++i)
    AllocatedFrameSlots.push_back(i);
  // Add callee-save objects.
  for (int i = MinCSFrameIndex; i <= (int)MaxCSFrameIndex; ++i)
    AllocatedFrameSlots.push_back(i);

  for (int i : AllocatedFrameSlots) {
    // These are converted from int64_t, but they should always fit in int
    // because of the FixedCSEnd check above.
    int ObjOffset = MFI.getObjectOffset(i);
    int ObjSize = MFI.getObjectSize(i);
    int ObjStart, ObjEnd;
    if (StackGrowsDown) {
      // ObjOffset is negative when StackGrowsDown is true.
      ObjStart = -ObjOffset - ObjSize;
      ObjEnd = -ObjOffset;
    } else {
      ObjStart = ObjOffset;
      ObjEnd = ObjOffset + ObjSize;
    }
    // Ignore fixed holes that are in the previous stack frame.
    if (ObjEnd > 0)
      StackBytesFree.reset(ObjStart, ObjEnd);
  }
}

/// Assign frame object to an unused portion of the stack in the fixed stack
/// object range.  Return true if the allocation was successful.
///
static inline bool scavengeStackSlot(MachineFrameInfo &MFI, int FrameIdx,
                                     bool StackGrowsDown, unsigned MaxAlign,
                                     BitVector &StackBytesFree) {
  if (MFI.isVariableSizedObjectIndex(FrameIdx))
    return false;

  if (StackBytesFree.none()) {
    // clear it to speed up later scavengeStackSlot calls to
    // StackBytesFree.none()
    StackBytesFree.clear();
    return false;
  }

  unsigned ObjAlign = MFI.getObjectAlignment(FrameIdx);
  if (ObjAlign > MaxAlign)
    return false;

  int64_t ObjSize = MFI.getObjectSize(FrameIdx);
  int FreeStart;
  for (FreeStart = StackBytesFree.find_first(); FreeStart != -1;
       FreeStart = StackBytesFree.find_next(FreeStart)) {

    // Check that free space has suitable alignment.
    unsigned ObjStart = StackGrowsDown ? FreeStart + ObjSize : FreeStart;
    if (alignTo(ObjStart, ObjAlign) != ObjStart)
      continue;

    if (FreeStart + ObjSize > StackBytesFree.size())
      return false;

    bool AllBytesFree = true;
    for (unsigned Byte = 0; Byte < ObjSize; ++Byte)
      if (!StackBytesFree.test(FreeStart + Byte)) {
        AllBytesFree = false;
        break;
      }
    if (AllBytesFree)
      break;
  }

  if (FreeStart == -1)
    return false;

  if (StackGrowsDown) {
    int ObjStart = -(FreeStart + ObjSize);
    DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") scavenged at SP[" << ObjStart
                 << "]\n");
    MFI.setObjectOffset(FrameIdx, ObjStart);
  } else {
    DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") scavenged at SP[" << FreeStart
                 << "]\n");
    MFI.setObjectOffset(FrameIdx, FreeStart);
  }

  StackBytesFree.reset(FreeStart, FreeStart + ObjSize);
  return true;
}

/// AssignProtectedObjSet - Helper function to assign large stack objects (i.e.,
/// those required to be close to the Stack Protector) to stack offsets.
static void
AssignProtectedObjSet(const StackObjSet &UnassignedObjs,
                      SmallSet<int, 16> &ProtectedObjs,
                      MachineFrameInfo &MFI, bool StackGrowsDown,
                      int64_t &Offset, unsigned &MaxAlign, unsigned Skew) {

  for (StackObjSet::const_iterator I = UnassignedObjs.begin(),
        E = UnassignedObjs.end(); I != E; ++I) {
    int i = *I;
    AdjustStackOffset(MFI, i, StackGrowsDown, Offset, MaxAlign, Skew);
    ProtectedObjs.insert(i);
  }
}

/// calculateFrameObjectOffsets - Calculate actual frame offsets for all of the
/// abstract stack objects.
///
void PEI::calculateFrameObjectOffsets(MachineFunction &Fn) {
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  StackProtector *SP = &getAnalysis<StackProtector>();

  bool StackGrowsDown =
    TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  // Loop over all of the stack objects, assigning sequential addresses...
  MachineFrameInfo &MFI = Fn.getFrameInfo();

  // Start at the beginning of the local area.
  // The Offset is the distance from the stack top in the direction
  // of stack growth -- so it's always nonnegative.
  int LocalAreaOffset = TFI.getOffsetOfLocalArea();
  if (StackGrowsDown)
    LocalAreaOffset = -LocalAreaOffset;
  assert(LocalAreaOffset >= 0
         && "Local area offset should be in direction of stack growth");
  int64_t Offset = LocalAreaOffset;

  // Skew to be applied to alignment.
  unsigned Skew = TFI.getStackAlignmentSkew(Fn);

  // If there are fixed sized objects that are preallocated in the local area,
  // non-fixed objects can't be allocated right at the start of local area.
  // Adjust 'Offset' to point to the end of last fixed sized preallocated
  // object.
  for (int i = MFI.getObjectIndexBegin(); i != 0; ++i) {
    int64_t FixedOff;
    if (StackGrowsDown) {
      // The maximum distance from the stack pointer is at lower address of
      // the object -- which is given by offset. For down growing stack
      // the offset is negative, so we negate the offset to get the distance.
      FixedOff = -MFI.getObjectOffset(i);
    } else {
      // The maximum distance from the start pointer is at the upper
      // address of the object.
      FixedOff = MFI.getObjectOffset(i) + MFI.getObjectSize(i);
    }
    if (FixedOff > Offset) Offset = FixedOff;
  }

  // First assign frame offsets to stack objects that are used to spill
  // callee saved registers.
  if (StackGrowsDown) {
    for (unsigned i = MinCSFrameIndex; i <= MaxCSFrameIndex; ++i) {
      // If the stack grows down, we need to add the size to find the lowest
      // address of the object.
      Offset += MFI.getObjectSize(i);

      unsigned Align = MFI.getObjectAlignment(i);
      // Adjust to alignment boundary
      Offset = alignTo(Offset, Align, Skew);

      DEBUG(dbgs() << "alloc FI(" << i << ") at SP[" << -Offset << "]\n");
      MFI.setObjectOffset(i, -Offset);        // Set the computed offset
    }
  } else if (MaxCSFrameIndex >= MinCSFrameIndex) {
    // Be careful about underflow in comparisons agains MinCSFrameIndex.
    for (unsigned i = MaxCSFrameIndex; i != MinCSFrameIndex - 1; --i) {
      if (MFI.isDeadObjectIndex(i))
        continue;

      unsigned Align = MFI.getObjectAlignment(i);
      // Adjust to alignment boundary
      Offset = alignTo(Offset, Align, Skew);

      DEBUG(dbgs() << "alloc FI(" << i << ") at SP[" << Offset << "]\n");
      MFI.setObjectOffset(i, Offset);
      Offset += MFI.getObjectSize(i);
    }
  }

  // FixedCSEnd is the stack offset to the end of the fixed and callee-save
  // stack area.
  int64_t FixedCSEnd = Offset;
  unsigned MaxAlign = MFI.getMaxAlignment();

  // Make sure the special register scavenging spill slot is closest to the
  // incoming stack pointer if a frame pointer is required and is closer
  // to the incoming rather than the final stack pointer.
  const TargetRegisterInfo *RegInfo = Fn.getSubtarget().getRegisterInfo();
  bool EarlyScavengingSlots = (TFI.hasFP(Fn) &&
                               TFI.isFPCloseToIncomingSP() &&
                               RegInfo->useFPForScavengingIndex(Fn) &&
                               !RegInfo->needsStackRealignment(Fn));
  if (RS && EarlyScavengingSlots) {
    SmallVector<int, 2> SFIs;
    RS->getScavengingFrameIndices(SFIs);
    for (SmallVectorImpl<int>::iterator I = SFIs.begin(),
           IE = SFIs.end(); I != IE; ++I)
      AdjustStackOffset(MFI, *I, StackGrowsDown, Offset, MaxAlign, Skew);
  }

  // FIXME: Once this is working, then enable flag will change to a target
  // check for whether the frame is large enough to want to use virtual
  // frame index registers. Functions which don't want/need this optimization
  // will continue to use the existing code path.
  if (MFI.getUseLocalStackAllocationBlock()) {
    unsigned Align = MFI.getLocalFrameMaxAlign();

    // Adjust to alignment boundary.
    Offset = alignTo(Offset, Align, Skew);

    DEBUG(dbgs() << "Local frame base offset: " << Offset << "\n");

    // Resolve offsets for objects in the local block.
    for (unsigned i = 0, e = MFI.getLocalFrameObjectCount(); i != e; ++i) {
      std::pair<int, int64_t> Entry = MFI.getLocalFrameObjectMap(i);
      int64_t FIOffset = (StackGrowsDown ? -Offset : Offset) + Entry.second;
      DEBUG(dbgs() << "alloc FI(" << Entry.first << ") at SP[" <<
            FIOffset << "]\n");
      MFI.setObjectOffset(Entry.first, FIOffset);
    }
    // Allocate the local block
    Offset += MFI.getLocalFrameSize();

    MaxAlign = std::max(Align, MaxAlign);
  }

  // Retrieve the Exception Handler registration node.
  int EHRegNodeFrameIndex = INT_MAX;
  if (const WinEHFuncInfo *FuncInfo = Fn.getWinEHFuncInfo())
    EHRegNodeFrameIndex = FuncInfo->EHRegNodeFrameIndex;

  // Make sure that the stack protector comes before the local variables on the
  // stack.
  SmallSet<int, 16> ProtectedObjs;
  if (MFI.getStackProtectorIndex() >= 0) {
    StackObjSet LargeArrayObjs;
    StackObjSet SmallArrayObjs;
    StackObjSet AddrOfObjs;

    AdjustStackOffset(MFI, MFI.getStackProtectorIndex(), StackGrowsDown,
                      Offset, MaxAlign, Skew);

    // Assign large stack objects first.
    for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
      if (MFI.isObjectPreAllocated(i) &&
          MFI.getUseLocalStackAllocationBlock())
        continue;
      if (i >= MinCSFrameIndex && i <= MaxCSFrameIndex)
        continue;
      if (RS && RS->isScavengingFrameIndex((int)i))
        continue;
      if (MFI.isDeadObjectIndex(i))
        continue;
      if (MFI.getStackProtectorIndex() == (int)i ||
          EHRegNodeFrameIndex == (int)i)
        continue;

      switch (SP->getSSPLayout(MFI.getObjectAllocation(i))) {
      case StackProtector::SSPLK_None:
        continue;
      case StackProtector::SSPLK_SmallArray:
        SmallArrayObjs.insert(i);
        continue;
      case StackProtector::SSPLK_AddrOf:
        AddrOfObjs.insert(i);
        continue;
      case StackProtector::SSPLK_LargeArray:
        LargeArrayObjs.insert(i);
        continue;
      }
      llvm_unreachable("Unexpected SSPLayoutKind.");
    }

    AssignProtectedObjSet(LargeArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign, Skew);
    AssignProtectedObjSet(SmallArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign, Skew);
    AssignProtectedObjSet(AddrOfObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign, Skew);
  }

  SmallVector<int, 8> ObjectsToAllocate;

  // Then prepare to assign frame offsets to stack objects that are not used to
  // spill callee saved registers.
  for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
    if (MFI.isObjectPreAllocated(i) && MFI.getUseLocalStackAllocationBlock())
      continue;
    if (i >= MinCSFrameIndex && i <= MaxCSFrameIndex)
      continue;
    if (RS && RS->isScavengingFrameIndex((int)i))
      continue;
    if (MFI.isDeadObjectIndex(i))
      continue;
    if (MFI.getStackProtectorIndex() == (int)i ||
        EHRegNodeFrameIndex == (int)i)
      continue;
    if (ProtectedObjs.count(i))
      continue;

    // Add the objects that we need to allocate to our working set.
    ObjectsToAllocate.push_back(i);
  }

  // Allocate the EH registration node first if one is present.
  if (EHRegNodeFrameIndex != INT_MAX)
    AdjustStackOffset(MFI, EHRegNodeFrameIndex, StackGrowsDown, Offset,
                      MaxAlign, Skew);

  // Give the targets a chance to order the objects the way they like it.
  if (Fn.getTarget().getOptLevel() != CodeGenOpt::None &&
      Fn.getTarget().Options.StackSymbolOrdering)
    TFI.orderFrameObjects(Fn, ObjectsToAllocate);

  // Keep track of which bytes in the fixed and callee-save range are used so we
  // can use the holes when allocating later stack objects.  Only do this if
  // stack protector isn't being used and the target requests it and we're
  // optimizing.
  BitVector StackBytesFree;
  if (!ObjectsToAllocate.empty() &&
      Fn.getTarget().getOptLevel() != CodeGenOpt::None &&
      MFI.getStackProtectorIndex() < 0 && TFI.enableStackSlotScavenging(Fn))
    computeFreeStackSlots(MFI, StackGrowsDown, MinCSFrameIndex, MaxCSFrameIndex,
                          FixedCSEnd, StackBytesFree);

  // Now walk the objects and actually assign base offsets to them.
  for (auto &Object : ObjectsToAllocate)
    if (!scavengeStackSlot(MFI, Object, StackGrowsDown, MaxAlign,
                           StackBytesFree))
      AdjustStackOffset(MFI, Object, StackGrowsDown, Offset, MaxAlign, Skew);

  // Make sure the special register scavenging spill slot is closest to the
  // stack pointer.
  if (RS && !EarlyScavengingSlots) {
    SmallVector<int, 2> SFIs;
    RS->getScavengingFrameIndices(SFIs);
    for (SmallVectorImpl<int>::iterator I = SFIs.begin(),
           IE = SFIs.end(); I != IE; ++I)
      AdjustStackOffset(MFI, *I, StackGrowsDown, Offset, MaxAlign, Skew);
  }

  if (!TFI.targetHandlesStackFrameRounding()) {
    // If we have reserved argument space for call sites in the function
    // immediately on entry to the current function, count it as part of the
    // overall stack size.
    if (MFI.adjustsStack() && TFI.hasReservedCallFrame(Fn))
      Offset += MFI.getMaxCallFrameSize();

    // Round up the size to a multiple of the alignment.  If the function has
    // any calls or alloca's, align to the target's StackAlignment value to
    // ensure that the callee's frame or the alloca data is suitably aligned;
    // otherwise, for leaf functions, align to the TransientStackAlignment
    // value.
    unsigned StackAlign;
    if (MFI.adjustsStack() || MFI.hasVarSizedObjects() ||
        (RegInfo->needsStackRealignment(Fn) && MFI.getObjectIndexEnd() != 0))
      StackAlign = TFI.getStackAlignment();
    else
      StackAlign = TFI.getTransientStackAlignment();

    // If the frame pointer is eliminated, all frame offsets will be relative to
    // SP not FP. Align to MaxAlign so this works.
    StackAlign = std::max(StackAlign, MaxAlign);
    Offset = alignTo(Offset, StackAlign, Skew);
  }

  // Update frame info to pretend that this is part of the stack...
  int64_t StackSize = Offset - LocalAreaOffset;
  MFI.setStackSize(StackSize);
  NumBytesStackSpace += StackSize;

  MachineOptimizationRemarkAnalysis R(
      DEBUG_TYPE, "StackSize", Fn.getFunction()->getSubprogram(), &Fn.front());
  R << ore::NV("NumStackBytes", StackSize)
    << " stack bytes in function";
  ORE->emit(R);
}

/// insertPrologEpilogCode - Scan the function for modified callee saved
/// registers, insert spill code for these callee saved registers, then add
/// prolog and epilog code to the function.
///
void PEI::insertPrologEpilogCode(MachineFunction &Fn) {
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = Fn.getFrameInfo();
  MachineOptimizationRemarkAnalysis R(DEBUG_TYPE, "PECount", {}, &Fn.front());

  // Add prologue to the function...
  auto TProl = TFI.getPrologues(Fn);
  auto *SaveB = MFI.getShouldUseStackShrinkWrap2() && !TProl.empty()
                    ? &TProl
                    : &SaveBlocks;
  R << "Shrink-wrapped stack with "
    << ore::NV("PrologueNum", static_cast<unsigned>(SaveB->size()))
    << " prologues.";
  for (MachineBasicBlock *SaveBlock : *SaveB)
    TFI.emitPrologue(Fn, *SaveBlock);

  // Add epilogue to restore the callee-save registers in each exiting block.
  auto TEpil = TFI.getEpilogues(Fn);
  auto *RestoreB = MFI.getShouldUseStackShrinkWrap2() && !TEpil.empty()
                       ? &TEpil
                       : &RestoreBlocks;
  R << "Shrink-wrapped stack with "
    << ore::NV("EpilogueNum", static_cast<unsigned>(RestoreB->size()))
    << " epilogues.";
  for (MachineBasicBlock *RestoreBlock : *RestoreB)
    TFI.emitEpilogue(Fn, *RestoreBlock);
  ORE->emit(R);

  // FIXME: ShrinkWrap2: Will this still work?
  for (MachineBasicBlock *SaveBlock : SaveBlocks)
    TFI.inlineStackProbe(Fn, *SaveBlock);

  // FIXME: ShrinkWrap2: Will this still work?
  // Emit additional code that is required to support segmented stacks, if
  // we've been asked for it.  This, when linked with a runtime with support
  // for segmented stacks (libgcc is one), will result in allocating stack
  // space in small chunks instead of one large contiguous block.
  if (Fn.shouldSplitStack()) {
    for (MachineBasicBlock *SaveBlock : SaveBlocks)
      TFI.adjustForSegmentedStacks(Fn, *SaveBlock);
  }

  // FIXME: ShrinkWrap2: Will this still work?
  // Emit additional code that is required to explicitly handle the stack in
  // HiPE native code (if needed) when loaded in the Erlang/OTP runtime. The
  // approach is rather similar to that of Segmented Stacks, but it uses a
  // different conditional check and another BIF for allocating more stack
  // space.
  if (Fn.getFunction()->getCallingConv() == CallingConv::HiPE)
    for (MachineBasicBlock *SaveBlock : SaveBlocks)
      TFI.adjustForHiPEPrologue(Fn, *SaveBlock);
}

/// replaceFrameIndices - Replace all MO_FrameIndex operands with physical
/// register references and actual offsets.
///
void PEI::replaceFrameIndices(MachineFunction &Fn) {
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  if (!TFI.needsFrameIndexResolution(Fn)) return;

  // Store SPAdj at exit of a basic block.
  SmallVector<int, 8> SPState;
  SPState.resize(Fn.getNumBlockIDs());
  df_iterator_default_set<MachineBasicBlock*> Reachable;

  // Iterate over the reachable blocks in DFS order.
  for (auto DFI = df_ext_begin(&Fn, Reachable), DFE = df_ext_end(&Fn, Reachable);
       DFI != DFE; ++DFI) {
    int SPAdj = 0;
    // Check the exit state of the DFS stack predecessor.
    if (DFI.getPathLength() >= 2) {
      MachineBasicBlock *StackPred = DFI.getPath(DFI.getPathLength() - 2);
      assert(Reachable.count(StackPred) &&
             "DFS stack predecessor is already visited.\n");
      SPAdj = SPState[StackPred->getNumber()];
    }
    MachineBasicBlock *BB = *DFI;
    replaceFrameIndices(BB, Fn, SPAdj);
    SPState[BB->getNumber()] = SPAdj;
  }

  // Handle the unreachable blocks.
  for (auto &BB : Fn) {
    if (Reachable.count(&BB))
      // Already handled in DFS traversal.
      continue;
    int SPAdj = 0;
    replaceFrameIndices(&BB, Fn, SPAdj);
  }
}

void PEI::replaceFrameIndices(MachineBasicBlock *BB, MachineFunction &Fn,
                              int &SPAdj) {
  assert(Fn.getSubtarget().getRegisterInfo() &&
         "getRegisterInfo() must be implemented!");
  const TargetInstrInfo &TII = *Fn.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *Fn.getSubtarget().getRegisterInfo();
  const TargetFrameLowering *TFI = Fn.getSubtarget().getFrameLowering();

  if (RS && FrameIndexEliminationScavenging)
    RS->enterBasicBlock(*BB);

  bool InsideCallSequence = false;

  for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {

    if (TII.isFrameInstr(*I)) {
      InsideCallSequence = TII.isFrameSetup(*I);
      SPAdj += TII.getSPAdjust(*I);
      I = TFI->eliminateCallFramePseudoInstr(Fn, *BB, I);
      continue;
    }

    MachineInstr &MI = *I;
    bool DoIncr = true;
    bool DidFinishLoop = true;
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      if (!MI.getOperand(i).isFI())
        continue;

      // Frame indices in debug values are encoded in a target independent
      // way with simply the frame index and offset rather than any
      // target-specific addressing mode.
      if (MI.isDebugValue()) {
        assert(i == 0 && "Frame indices can only appear as the first "
                         "operand of a DBG_VALUE machine instruction");
        unsigned Reg;
        int64_t Offset =
            TFI->getFrameIndexReference(Fn, MI.getOperand(0).getIndex(), Reg);
        MI.getOperand(0).ChangeToRegister(Reg, false /*isDef*/);
        auto *DIExpr = DIExpression::prepend(MI.getDebugExpression(),
                                             DIExpression::NoDeref, Offset);
        MI.getOperand(3).setMetadata(DIExpr);
        continue;
      }

      // TODO: This code should be commoned with the code for
      // PATCHPOINT. There's no good reason for the difference in
      // implementation other than historical accident.  The only
      // remaining difference is the unconditional use of the stack
      // pointer as the base register.
      if (MI.getOpcode() == TargetOpcode::STATEPOINT) {
        assert((!MI.isDebugValue() || i == 0) &&
               "Frame indicies can only appear as the first operand of a "
               "DBG_VALUE machine instruction");
        unsigned Reg;
        MachineOperand &Offset = MI.getOperand(i + 1);
        int refOffset = TFI->getFrameIndexReferencePreferSP(
            Fn, MI.getOperand(i).getIndex(), Reg, /*IgnoreSPUpdates*/ false);
        Offset.setImm(Offset.getImm() + refOffset);
        MI.getOperand(i).ChangeToRegister(Reg, false /*isDef*/);
        continue;
      }

      // Some instructions (e.g. inline asm instructions) can have
      // multiple frame indices and/or cause eliminateFrameIndex
      // to insert more than one instruction. We need the register
      // scavenger to go through all of these instructions so that
      // it can update its register information. We keep the
      // iterator at the point before insertion so that we can
      // revisit them in full.
      bool AtBeginning = (I == BB->begin());
      if (!AtBeginning) --I;

      // If this instruction has a FrameIndex operand, we need to
      // use that target machine register info object to eliminate
      // it.
      TRI.eliminateFrameIndex(MI, SPAdj, i,
                              FrameIndexEliminationScavenging ?  RS : nullptr);

      // Reset the iterator if we were at the beginning of the BB.
      if (AtBeginning) {
        I = BB->begin();
        DoIncr = false;
      }

      DidFinishLoop = false;
      break;
    }

    // If we are looking at a call sequence, we need to keep track of
    // the SP adjustment made by each instruction in the sequence.
    // This includes both the frame setup/destroy pseudos (handled above),
    // as well as other instructions that have side effects w.r.t the SP.
    // Note that this must come after eliminateFrameIndex, because
    // if I itself referred to a frame index, we shouldn't count its own
    // adjustment.
    if (DidFinishLoop && InsideCallSequence)
      SPAdj += TII.getSPAdjust(MI);

    if (DoIncr && I != BB->end()) ++I;

    // Update register states.
    if (RS && FrameIndexEliminationScavenging && DidFinishLoop)
      RS->forward(MI);
  }
}
