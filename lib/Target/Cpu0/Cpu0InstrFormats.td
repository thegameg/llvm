//===-- Cpu0InstrFormats.td - Cpu0 Instruction Formats -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe Cpu0 instructions format
//
//  CPU INSTRUCTION FORMATS
//
//  opcode  - operation code.
//  ra      - dst reg only on a 3 reg instr.
//  rb      - src reg.
//  rc      - src reg, only used on 3 regs instr.
//  cx      - imm
//
//===----------------------------------------------------------------------===//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<2> val> {
  bits<2> Value = val;
}

def A : Format<0>;
def L : Format<1>;
def J : Format<2>;

// Generic Cpu0 Format
class Cpu0Inst<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
               InstrItinClass itin, Format f> : Instruction {
  bits<32> Inst;
  Format Form = f;
  let Inst{31 - 24} = op;

  let Namespace = "Cpu0";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = 4;
  let DecoderNamespace = "Cpu0";
  let Itinerary = itin;
}

//===----------------------------------------------------------------------===//
// Format A instruction class in Cpu0 : <|opcode|ra|rb|rc|cx|>
//===----------------------------------------------------------------------===//

class FA<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>
    : Cpu0Inst<op, outs, ins, asmstr, pattern, itin, A> {
  bits<4> ra;
  bits<4> rb;
  bits<4> rc;
  bits<12> cx;

  let Inst{23 - 20} = ra;
  let Inst{19 - 16} = rb;
  let Inst{15 - 12} = rc;
  let Inst{11 - 0} = cx;
}

//===----------------------------------------------------------------------===//
// Format L instruction class in Cpu0 : <|opcode|ra|rb|cx|>
//===----------------------------------------------------------------------===//

class FL<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>
    : Cpu0Inst<op, outs, ins, asmstr, pattern, itin, L> {
  bits<4> ra;
  bits<4> rb;
  bits<16> cx;

  let Inst{23 - 20} = ra;
  let Inst{19 - 16} = rb;
  let Inst{15 - 0} = cx;
}

//===----------------------------------------------------------------------===//
// Format J instruction class in Cpu0 : <|opcode|ra|cx|>
//===----------------------------------------------------------------------===//

class FJ<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>
    : Cpu0Inst<op, outs, ins, asmstr, pattern, itin, J> {
  bits<24> cx;

  let Inst{23 - 0} = cx;
}
