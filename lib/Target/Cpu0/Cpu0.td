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
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "Cpu0RegisterInfo.td"

include "Cpu0InstrInfo.td"
def Cpu0InstrInfo : InstrInfo;

include "Cpu0CallingConv.td"

//===----------------------------------------------------------------------===//
// Cpu0 processors supported.
//===----------------------------------------------------------------------===//

include "Cpu0Schedule.td"

// FIXME: Add scheduling.
def : ProcessorModel<"cpu0", NoSchedModel, []>;

//===----------------------------------------------------------------------===//
// The target.
//===----------------------------------------------------------------------===//

def Cpu0 : Target {
  let InstructionSet = Cpu0InstrInfo;
  // AsmParser and AsmWriter are kept default.
}
