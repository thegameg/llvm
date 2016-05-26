//===-- Cpu0TargetMachine.cpp - Define TargetMachine for Cpu0 00-----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "Cpu0TargetMachine.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

extern "C" void LLVMInitializeCpu0Target() {
  // Register the target.
  RegisterTargetMachine<Cpu0TargetMachine> X(TheCpu0Target);
}

static std::string computeDataLayout(const Triple &TT) {
  return "e-p:32:32";
}

Cpu0TargetMachine::Cpu0TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     Reloc::Model RM, CodeModel::Model CM,
                                     CodeGenOpt::Level OL)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options, RM, CM,
                        OL)
{
}
