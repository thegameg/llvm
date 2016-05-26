//===-- Cpu0InstrInfo.td -----------------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//

include "Cpu0InstrFormats.td"

//===----------------------------------------------------------------------===//
// Describe constraints
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Describe instructions
//===----------------------------------------------------------------------===//

// FIXME: Fix itineraries.

def NOP   : FJ<0x0, (outs), (ins), "nop", [], NoItinerary>;

def ADDrr : FL<0x13, (outs CPURegs:$ra), (ins CPURegs:$rb, CPURegs:$rc),
               "add\t$ra, $rb, $rc", [(set CPURegs:$ra,
               (add CPURegs:$rb, CPURegs:$rc))], NoItinerary>;
