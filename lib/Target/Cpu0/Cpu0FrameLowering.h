//===-- Cpu0FrameLowering.h - Define frame lowering for Cpu0 ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Cpu0 frame lowering.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Cpu0_Cpu0FRAMELOWERING_H
#define LLVM_LIB_TARGET_Cpu0_Cpu0FRAMELOWERING_H

#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class Cpu0FrameLowering : public TargetFrameLowering {
public:
  explicit Cpu0FrameLowering(unsigned Alignment)
      : TargetFrameLowering(StackGrowsDown, Alignment, 0, Alignment) {}

  bool hasFP(const MachineFunction &MF) const override { return true; }
  bool hasBP(const MachineFunction &MF) const { return false; }

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
};

} // End llvm namespace

#endif
