//===-- Cpu0InstrInfo.td -----------------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//

include "Cpu0InstrFormats.td"

//===----------------------------------------------------------------------===//
// Describe Cpu0 constraints
//===----------------------------------------------------------------------===//

def imm32 : Operand<i32>;

//===----------------------------------------------------------------------===//
// Describe IR constraints
//===----------------------------------------------------------------------===//

def imm32Pat : PatLeaf<(imm), [{ return isInt<32>(N->getSExtValue()); }]>;

//===----------------------------------------------------------------------===//
// Cpu0 profiles and nodes
//===----------------------------------------------------------------------===//

def Cpu0Ret : SDNode<"Cpu0ISD::Ret", SDTNone,
                     [ SDNPHasChain, SDNPOptInGlue, SDNPVariadic ]>;

//===----------------------------------------------------------------------===//
// Describe instructions
//===----------------------------------------------------------------------===//

// FIXME: Fix itineraries.

def NOP   : FN<0x0, "nop", [], NoItinerary>;

let isReturn = 1, isTerminator = 1 in {
  // FIXME: Add alias on jr %lr.
  def Ret : FN<0x3c, "ret", [ (Cpu0Ret) ], NoItinerary>;
}

def ADDrr : FL<0x13, (outs CPURegs:$ra), (ins CPURegs:$rb, CPURegs:$rc),
               "add\t$ra, $rb, $rc", [(set CPURegs:$ra,
               (add CPURegs:$rb, CPURegs:$rc))], NoItinerary>;

def ADDri : FL<0x09, (outs CPURegs:$ra), (ins CPURegs:$rb, imm32:$c),
               "add\t$ra, $rb, $c", [(set CPURegs:$ra,
               (add CPURegs:$rb, imm32Pat:$c))], NoItinerary>;

def SUBrr : FL<0x14, (outs CPURegs:$ra), (ins CPURegs:$rb, CPURegs:$rc),
               "sub\t$ra, $rb, $rc", [(set CPURegs:$ra,
               (sub CPURegs:$rb, CPURegs:$rc))], NoItinerary>;

def : Pat<(i32 imm32Pat:$in), (ADDri ZERO, imm:$in)>;
