//===-- Cpu0Subtarget.cpp - Cpu0 Subtarget Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cpu0 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//
#include "Cpu0Subtarget.h"
#include "Cpu0RegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "cpu0-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "Cpu0GenSubtargetInfo.inc"

Cpu0Subtarget::Cpu0Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                             const Cpu0TargetMachine &TM)
    : Cpu0GenSubtargetInfo(TT, CPU, FS), TargetTriple{TT}, FrameLowering{8},
      TargetLowering{TM}, InstrInfo{*this}, RegisterInfo{} {}
