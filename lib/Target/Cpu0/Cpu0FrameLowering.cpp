//===-- Cpu0FrameLowering.cpp - Cpu0 Frame Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cpu0 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "Cpu0FrameLowering.h"

using namespace llvm;

void Cpu0FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  return;
}

void Cpu0FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  return;
}
