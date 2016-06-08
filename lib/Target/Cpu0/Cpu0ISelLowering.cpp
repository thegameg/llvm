//===-- Cpu0ISelLowering.cpp - Cpu0 DAG Lowering Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Cpu0 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "Cpu0ISelLowering.h"
#include "Cpu0TargetMachine.h"

using namespace llvm;

Cpu0TargetLowering::Cpu0TargetLowering(const Cpu0TargetMachine &TM)
    : TargetLowering(TM) {}
