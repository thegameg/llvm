//===-- Cpu0RegisterInfo.cpp - Cpu0 Register Information -== --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cpu0 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "Cpu0RegisterInfo.h"
#include "Cpu0FrameLowering.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetSubtargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "Cpu0-reg-info"

#define GET_REGINFO_TARGET_DESC
#define GET_REGINFO_ENUM
#include "Cpu0GenRegisterInfo.inc"

// LR is the register containing the return address.
Cpu0RegisterInfo::Cpu0RegisterInfo() : Cpu0GenRegisterInfo(Cpu0::LR) {}

const MCPhysReg *
Cpu0RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_SaveList;
}

unsigned Cpu0RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return Cpu0::FP;
}

BitVector Cpu0RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  // FIXME: Use TableGen.
  static const MCPhysReg ReservedGPR[] = {Cpu0::ZERO, Cpu0::GP, Cpu0::FP,
                                          Cpu0::SP,   Cpu0::LR, Cpu0::SW,
                                          Cpu0::PC,   Cpu0::EPC};

  BitVector Reserved(getNumRegs());

  for (unsigned I = 0; I < array_lengthof(ReservedGPR); ++I)
    Reserved.set(ReservedGPR[I]);

  return Reserved;
}

// FrameIndex represent objects inside a abstract stack.
// We must replace FrameIndex with an stack/frame pointer
// direct reference.
void Cpu0RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  // FIXME: Eliminate them.
  return;
}
