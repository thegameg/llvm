//===- CSRFIShrinkWrapInfo.cpp - Shrink Wrapping Utility -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Implementation of the CSRFIShrinkWrapInfo.
//===----------------------------------------------------------------------===//

#include "CSRFIShrinkWrapInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

STATISTIC(NumFunc, "Number of functions");

const CSRFIShrinkWrapInfo::SetOfRegs &
CSRFIShrinkWrapInfo::getCurrentCSRs(RegScavenger *RS) const {
  if (CurrentCSRs.empty()) {
    BitVector SavedRegs;
    const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

    TFI->determineCalleeSaves(const_cast<MachineFunction &>(MF), SavedRegs, RS);

    for (int Reg = SavedRegs.find_first(); Reg != -1;
         Reg = SavedRegs.find_next(Reg))
      CurrentCSRs.insert((unsigned)Reg);
  }
  return CurrentCSRs;
}

bool CSRFIShrinkWrapInfo::useOrDefCSROrFI(const MachineInstr &MI,
                                          RegScavenger *RS) const {
  if (MI.getOpcode() == FrameSetupOpcode ||
      MI.getOpcode() == FrameDestroyOpcode) {
    DEBUG(dbgs() << "Frame instruction: " << MI << '\n');
    return true;
  }
  for (const MachineOperand &MO : MI.operands()) {
    bool UseOrDefCSR = false;
    if (MO.isReg()) {
      // Ignore instructions like DBG_VALUE which don't read/def the register.
      if (!MO.isDef() && !MO.readsReg())
        continue;
      unsigned PhysReg = MO.getReg();
      if (!PhysReg)
        continue;
      assert(TargetRegisterInfo::isPhysicalRegister(PhysReg) &&
             "Unallocated register?!");
      UseOrDefCSR = RCI.getLastCalleeSavedAlias(PhysReg);
    } else if (MO.isRegMask()) {
      // Check if this regmask clobbers any of the CSRs.
      for (unsigned Reg : getCurrentCSRs(RS)) {
        if (MO.clobbersPhysReg(Reg)) {
          UseOrDefCSR = true;
          break;
        }
      }
    }
    // Skip FrameIndex operands in DBG_VALUE instructions.
    if (UseOrDefCSR || (MO.isFI() && !MI.isDebugValue())) {
      DEBUG(dbgs() << "Use or define CSR(" << UseOrDefCSR << ") or FI("
                   << MO.isFI() << "): " << MI << '\n');
      return true;
    }
  }
  return false;
}

CSRFIShrinkWrapInfo::CSRFIShrinkWrapInfo(const MachineFunction &MF,
                                         RegScavenger *RS)
    : ShrinkWrapInfo(), Uses(MF.getNumBlockIDs()), MF(MF),
      FrameSetupOpcode(
          MF.getSubtarget().getInstrInfo()->getCallFrameSetupOpcode()),
      FrameDestroyOpcode(
          MF.getSubtarget().getInstrInfo()->getCallFrameDestroyOpcode()) {
  ++NumFunc;

  RCI.runOnMachineFunction(MF);
  for (const MachineBasicBlock &MBB : MF) {
    DEBUG(dbgs() << "Check for uses: " << printMBBReference(MBB) << ' '
                 << MBB.getName() << '\n');

    if (MBB.isEHFuncletEntry()) {
      DEBUG(dbgs() << "EH Funclets are not supported yet.\n");
      Uses.clear();
      return;
    }
    for (const MachineInstr &MI :
         make_range(MBB.rbegin(), MBB.rend())) {
      if (useOrDefCSROrFI(MI, RS)) {
        Uses.set(MBB.getNumber());
        // Make sure we would be able to insert the restore code before the
        // terminator. Mark the successors as used as well, since we can't
        // restore after a terminator (i.e. cbz w23, #10).
        // Note: This doesn't handle the case when a return instruction uses a
        // CSR/FI. If that happens, the default placement in all the return
        // blocks won't work either.
        if (MI.isTerminator())
          for (const MachineBasicBlock *Succ : MBB.successors())
            Uses.set(Succ->getNumber());
        break;
      }
    }
  }
}

const BitVector *CSRFIShrinkWrapInfo::getUses(unsigned MBBNum) const {
  if (!Uses.empty() && Uses.test(MBBNum)) {
    // Create a dummy BitVector having only one bit which is set.
    static BitVector HasUses;
    if (HasUses.empty()) {
      HasUses.resize(CSRFIShrinkWrapInfoSize);
      HasUses.set(CSRFIUsedBit);
    }
    return &HasUses;
  }
  return nullptr;
}

const BitVector &CSRFIShrinkWrapInfo::getAllUsedElts() const {
  if (!Uses.empty() && Uses.any()) {
    // Create a dummy BitVector having only one bit which is set.
    static BitVector Set;
    if (Set.empty())
      Set.resize(CSRFIShrinkWrapInfoSize, true);
    return Set;
  }
  // Create a dummy BitVector having only one bit which is unset.
  static BitVector Unset;
  if (Unset.empty())
    Unset.resize(CSRFIShrinkWrapInfoSize, false);
  return Unset;
}
