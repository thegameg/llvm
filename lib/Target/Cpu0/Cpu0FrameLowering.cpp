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
#include "Cpu0Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#include "Cpu0GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "Cpu0GenInstrInfo.inc"

void Cpu0FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  auto &MFI = MF.getFrameInfo();
  auto StackSize = MFI.getStackSize();
  auto &TTI =
      *static_cast<const Cpu0Subtarget &>(MF.getSubtarget()).getInstrInfo();

  if (StackSize == 0) // Nothing to do.
    return;

  // This points to the first instruction saving callee-saved registers.
  auto it = MBB.begin();

  // Skip the instructions saving callee-saved registers.
  it = std::next(it, MFI.getCalleeSavedInfo().size());

  DebugLoc DL;
  if (isInt<16>(-StackSize))
    // add $sp, $sp, 0
    BuildMI(MBB, it, DL, TTI.get(Cpu0::ADDri), Cpu0::SP)
        .addReg(Cpu0::SP)
        .addImm(-StackSize);
  else {
    // FIXME: Implement it?
    llvm_unreachable("Not implemented yet");
  }

  // FIXME: Emit .cfi directives.
  if (hasFP(MF)) {
    // move $fp, $sp, alias to add $fp, $sp, 0
    BuildMI(MBB, it, DL, TTI.get(Cpu0::ADDri), Cpu0::FP)
        .addReg(Cpu0::SP)
        .addImm(0);
  }
}

void Cpu0FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  auto &MFI = MF.getFrameInfo();
  auto StackSize = MFI.getStackSize();

  if (StackSize == 0)
    return;

  auto &TTI =
      *static_cast<const Cpu0Subtarget &>(MF.getSubtarget()).getInstrInfo();

  // This points to the basic block terminator.
  auto it = MBB.getFirstTerminator();

  auto DL = it != MBB.end() ? it->getDebugLoc() : DebugLoc{};

  // Skip the instructions saving callee-saved registers.
  it = std::prev(it, MFI.getCalleeSavedInfo().size());

  // So, now, we insert the instructions in reverse order.
  if (hasFP(MF)) {
    // move $sp, $fp, alias to add $sp, $fp, 0
    BuildMI(MBB, it, DL, TTI.get(Cpu0::ADDri), Cpu0::SP)
        .addReg(Cpu0::FP)
        .addImm(0);
  }

  // FIXME: Emit .cfi directives.

  if (isInt<16>(StackSize))
    // add $sp, $sp, 0
    BuildMI(MBB, it, DL, TTI.get(Cpu0::ADDri), Cpu0::SP)
        .addReg(Cpu0::SP)
        .addImm(StackSize);
  else {
    // FIXME: Implement it?
    llvm_unreachable("Not implemented yet");
  }
}
