//===- PartialShrinkWrapInfo.cpp - Shrink Wrapping Utility ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Implementation of the PartialShrinkWrapInfo.
//===----------------------------------------------------------------------===//

#include "PartialShrinkWrapInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

PartialShrinkWrapInfo::PartialShrinkWrapInfo(const MachineFunction &MF,
                                             RegScavenger *RS)
    : ShrinkWrapInfo(), Uses(MF.getNumBlockIDs()),
      FrameSetupOpcode(
          MF.getSubtarget().getInstrInfo()->getCallFrameSetupOpcode()),
      FrameDestroyOpcode(
          MF.getSubtarget().getInstrInfo()->getCallFrameDestroyOpcode()) {

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Walk all the uses of each callee-saved register, and map them to their
  // basic blocks.
  const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();

  BitVector CSRegUnits(TRI.getNumRegUnits());
  DenseMap<unsigned, unsigned> RegUnitToCSRIdx;
  unsigned i = 0;
  for (i = 0; CSRegs[i]; ++i) {
    for (MCRegUnitIterator RegUnit(CSRegs[i], &TRI); RegUnit.isValid();
         ++RegUnit) {
      RegUnitToCSRIdx[*RegUnit] = i;
      CSRegUnits.set(*RegUnit);
    }
  }

  // Basically CSRegs.size(), but it's a null-terminated array.
  // + 1 for the stack bit.
  AllUsedElts.resize(i + 1);

  auto MarkAsUsedBase = [&](unsigned RegIdx, unsigned MBBNum) {
    BitVector &Used = Uses[MBBNum];
    if (Used.empty())
      Used.resize(getNumElts());
    Used.set(RegIdx);
    AllUsedElts.set(RegIdx);
  };

  auto MarkAsUsed = [&](unsigned RegIdx, const MachineBasicBlock &MBB,
                        bool isTerminator = false) {
    unsigned MBBNum = MBB.getNumber();
    MarkAsUsedBase(RegIdx, MBBNum);
    // Make sure we would be able to insert the restore code before the
    // terminator. Mark the successors as used as well, since we can't
    // save after a terminator (i.e. cbz w23, #10). This doesn't handle
    // the case when a return instruction uses a CSR. The default
    // placement won't work there either.
    if (isTerminator)
      for (MachineBasicBlock *Succ : MBB.successors())
        MarkAsUsedBase(RegIdx, Succ->getNumber());
  };

  for (const MachineBasicBlock &MBB : MF) {
    DEBUG(dbgs() << "Check for uses: " << printMBBReference(MBB) << ' '
                 << MBB.getName() << '\n');

    if (MBB.isEHFuncletEntry()) {
      DEBUG(dbgs() << "EH Funclets are not supported yet.\n");
      Uses.clear();
      return;
    }

    for (const MachineInstr &MI : MBB) {
      if (MI.getOpcode() == FrameSetupOpcode ||
          MI.getOpcode() == FrameDestroyOpcode) {
        DEBUG(dbgs() << "Frame instruction: " << MI << '\n');
        MarkAsUsed(StackBit, MBB, MI.isTerminator());
      }

      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isFI()) {
          DEBUG(dbgs() << "FI: " << MI << '\n');
          MarkAsUsed(StackBit, MBB, MI.isTerminator());
        } else if (MO.isRegMask()) {
          // Check for regmasks only on the original CSR, as the aliases are not
          // always there.
          for (unsigned i = 0; CSRegs[i]; ++i) {
            if (MO.clobbersPhysReg(CSRegs[i])) {
              DEBUG(dbgs() << "Clobers " << printReg(CSRegs[i], &TRI) << ": "
                           << MI << '\n');
              MarkAsUsed(FirstCSRBit + i, MBB, MI.isTerminator());
              // Also mark the stack as used.
              MarkAsUsed(StackBit, MBB, MI.isTerminator());
            }
          }
        } else if (MO.isReg() && MO.getReg() && (MO.readsReg() || MO.isDef())) {
          for (MCRegUnitIterator RegUnit(MO.getReg(), &TRI); RegUnit.isValid();
               ++RegUnit) {
            if (CSRegUnits.test(*RegUnit)) {
              DEBUG(dbgs() << "Uses "
                           << printReg(CSRegs[RegUnitToCSRIdx[*RegUnit]], &TRI)
                           << ": " << MI << '\n');
              MarkAsUsed(FirstCSRBit + RegUnitToCSRIdx[*RegUnit], MBB,
                         MI.isTerminator());
              // Also mark the stack as used.
              MarkAsUsed(StackBit, MBB, MI.isTerminator());
            }
          }
        }
      }
    }
  }
}

const BitVector *PartialShrinkWrapInfo::getUses(unsigned MBBNum) const {
  if (!Uses.empty())
    return &Uses[MBBNum];
  return nullptr;
}
