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
#include "Cpu0ISelDAGToDAG.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

extern "C" void LLVMInitializeCpu0Target() {
  // Register the target.
  RegisterTargetMachine<Cpu0TargetMachine> X(TheCpu0Target);
}

static std::string computeDataLayout(const Triple &TT) {
  return "e-p:32:32";
}

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::Static;
  return *RM;
}

Cpu0TargetMachine::Cpu0TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     Optional<Reloc::Model> RM,
                                     CodeModel::Model CM, CodeGenOpt::Level OL)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        getEffectiveRelocModel(RM), CM, OL),
      TLOF(make_unique<Cpu0TargetObjectFile>()) {
  initAsmInfo();
}

TargetPassConfig *Cpu0TargetMachine::createPassConfig(PassManagerBase &PM) {
  struct Cpu0PassConfig : public TargetPassConfig {
    Cpu0PassConfig(Cpu0TargetMachine *TM, PassManagerBase &PM)
        : TargetPassConfig(TM, PM) {}

    Cpu0TargetMachine &getCpu0TargetMachine() const {
      return getTM<Cpu0TargetMachine>();
    }

    bool addInstSelector() override {
      // FIXME : Add instruction selector.
      return false;
    }
  };

  return new Cpu0PassConfig(this, PM);
}
