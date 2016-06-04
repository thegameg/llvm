//===-- Cpu0AsmPrinter.cpp - Cpu0 Register Information -== ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Cpu0 Assembly printer class.
//
//===----------------------------------------------------------------------===//

#include "Cpu0AsmPrinter.h"
#include "Cpu0TargetMachine.h"
#include "InstPrinter/Cpu0InstPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

void Cpu0AsmPrinter::EmitFunctionEntryLabel() {
  OutStreamer->EmitLabel(CurrentFnSym);
}

void Cpu0AsmPrinter::EmitInstruction(const MachineInstr *MI) {
    MCInst TmpInst0;
    instLower.Lower(MI, TmpInst0);
    EmitToStreamer(*OutStreamer, TmpInst0);
}

extern "C" void LLVMInitializeCpu0AsmPrinter() {
  RegisterAsmPrinter<Cpu0AsmPrinter> X(TheCpu0Target);
}
