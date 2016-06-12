//===-- Cpu0InstrInfo.cpp - Cpu0 Instruction Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cpu0 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Cpu0InstrInfo.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "Cpu0GenInstrInfo.inc"

Cpu0InstrInfo::Cpu0InstrInfo(const Cpu0Subtarget &STI)
    : Cpu0GenInstrInfo{}, Subtarget{STI} {}
