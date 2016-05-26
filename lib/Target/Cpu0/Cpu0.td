//===-- Cpu0.td - Describe the Cpu0 Target Machine ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the Cpu0 target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File
//===----------------------------------------------------------------------===//

include "Cpu0RegisterInfo.td"

//===----------------------------------------------------------------------===//
// Instructions File
//===----------------------------------------------------------------------===//

include "Cpu0InstrInfo.td"
def Cpu0InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Cpu0 processors supported.
//===----------------------------------------------------------------------===//

// FIXME: Define processors.

//===----------------------------------------------------------------------===//
// AsmWriter and AsmParser.
//===----------------------------------------------------------------------===//

// FIXME: Add AsmWriter.

// FIXME: Add AsmParser.

def Cpu0 : Target {
  let InstructionSet = Cpu0InstrInfo;
  // FIXME: AsmParser.
  // FIXME: AsmWriter.
}
