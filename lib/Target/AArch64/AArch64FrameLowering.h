//==-- AArch64FrameLowering.h - TargetFrameLowering for AArch64 --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H

#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

namespace llvm {

class AArch64FrameLowering : public TargetFrameLowering {
public:
  explicit AArch64FrameLowering()
      : TargetFrameLowering(StackGrowsDown, 16, 0, 16,
                            true /*StackRealignable*/) {}

  void emitCalleeSavedFrameMoves(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI) const;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  // FIXME: ShrinkWrap2: Delay the computation of NumRegsSpilled.
  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;

  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;
  int resolveFrameIndexReference(const MachineFunction &MF, int FI,
                                 unsigned &FrameReg,
                                 bool PreferFP = false) const;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  std::vector<CalleeSavedInfo> &CSI,
                                  const TargetRegisterInfo *TRI) const override;

  /// \brief Can this function use the red zone for local allocations.
  bool canUseRedZone(const MachineFunction &MF) const;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return true;
  }

  bool enableStackSlotScavenging(const MachineFunction &MF) const override;

  // FIXME: ShrinkWrap2: We need this to call computeCalleeSaveRegisterParis
  // before we spill them.
  void
  processValidCalleeSavedInfo(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  // FIXME: ShrinkWrap2: Trick to cmpute stack-shrink-wrapping after we did csr
  // shrink-wrapping.
  void processSaveRestorePoints(MachineFunction &MF, CalleeSavedMap &Saves,
                                CalleeSavedMap &Restores) const override;
  std::unique_ptr<ShrinkWrapInfo>
  createCSRShrinkWrapInfo(const MachineFunction &MF) const override;

  SmallVector<MachineBasicBlock *, 4>
  getPrologues(MachineFunction &MF) const override;
  SmallVector<MachineBasicBlock *, 4>
  getEpilogues(MachineFunction &MF) const override;

private:
  bool shouldCombineCSRLocalStackBump(MachineFunction &MF,
                                      unsigned StackBumpBytes) const;
};

} // End llvm namespace

#endif
