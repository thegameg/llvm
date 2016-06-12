//===-- Cpu0Subtarget.h - Define Subtarget for the Cpu0 ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Cpu0 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Cpu0_Cpu0SUBTARGET_H
#define LLVM_LIB_TARGET_Cpu0_Cpu0SUBTARGET_H

#include "Cpu0FrameLowering.h"
#include "Cpu0ISelLowering.h"
#include "Cpu0InstrInfo.h"
#include "Cpu0RegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "Cpu0GenSubtargetInfo.inc"

namespace llvm {
class Cpu0TargetMachine;

class Cpu0Subtarget : public Cpu0GenSubtargetInfo {
  Triple TargetTriple;

  Cpu0FrameLowering FrameLowering;

  Cpu0TargetLowering TargetLowering;

  Cpu0InstrInfo InstrInfo;

  Cpu0RegisterInfo RegisterInfo;

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  Cpu0Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                const Cpu0TargetMachine &TM);

  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  const Cpu0FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }

  const Cpu0TargetLowering *getTargetLowering() const override {
    return &TargetLowering;
  }

  const Cpu0InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const Cpu0RegisterInfo *getRegisterInfo() const override {
    return &RegisterInfo;
  }
};
} // End llvm namespace

#endif
