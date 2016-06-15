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

#ifndef LLVM_LIB_TARGET_CPU0_CPU0ASMPRINTER_H
#define LLVM_LIB_TARGET_CPU0_CPU0ASMPRINTER_H

#include "Cpu0AsmPrinter.h"
#include "Cpu0MCInstLower.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class Cpu0AsmPrinter : public AsmPrinter {
  Cpu0MCInstLower instLower;

public:
  Cpu0AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  const char *getPassName() const override { return "Cpu0 Assembly Printer"; }

  void EmitInstruction(const MachineInstr *MI) override;
  void EmitFunctionEntryLabel() override;
};

} // namespace llvm

#endif
