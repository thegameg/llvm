//===-- Cpu0RegisterInfo.td --------------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Describe registers
//===----------------------------------------------------------------------===//

class Cpu0Reg<bits<16> Enc, string n> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "Cpu0";
}

class Cpu0GPRReg<bits<16> Enc, string n> : Cpu0Reg<Enc, n>;

class Cpu0C0Reg<bits<16> Enc, string n> : Cpu0Reg<Enc, n>;

//===----------------------------------------------------------------------===//
//@Registers
//===----------------------------------------------------------------------===//

let Namespace = "Cpu0" in {
  def ZERO : Cpu0GPRReg<0,  "zero">, DwarfRegNum<[0]>;
  def R1   : Cpu0GPRReg<1,  "1">,    DwarfRegNum<[1]>;
  def R2   : Cpu0GPRReg<2,  "2">,    DwarfRegNum<[2]>;
  def R3   : Cpu0GPRReg<3,  "3">,    DwarfRegNum<[3]>;
  def R4   : Cpu0GPRReg<4,  "4">,    DwarfRegNum<[4]>;
  def R5   : Cpu0GPRReg<5,  "5">,    DwarfRegNum<[5]>;
  def R6   : Cpu0GPRReg<6,  "6">,    DwarfRegNum<[6]>;
  def R7   : Cpu0GPRReg<7,  "7">,    DwarfRegNum<[7]>;
  def R8   : Cpu0GPRReg<8,  "8">,    DwarfRegNum<[8]>;
  def R9   : Cpu0GPRReg<9,  "9">,    DwarfRegNum<[9]>;
  def R10  : Cpu0GPRReg<10, "10">,   DwarfRegNum<[10]>;
  def GP   : Cpu0GPRReg<11, "gp">,   DwarfRegNum<[11]>;
  def FP   : Cpu0GPRReg<12, "fp">,   DwarfRegNum<[12]>;
  def SP   : Cpu0GPRReg<13, "sp">,   DwarfRegNum<[13]>;
  def LR   : Cpu0GPRReg<14, "lr">,   DwarfRegNum<[14]>; // Return address.
  def SW   : Cpu0GPRReg<15, "sw">,   DwarfRegNum<[15]>;

  def PC   : Cpu0C0Reg<0, "pc">, DwarfRegNum<[20]>;
  def EPC  : Cpu0C0Reg<0, "pc">, DwarfRegNum<[20]>; // Exception PC.
                                                    // Stores PC during
                                                    // exceptions.
}

//===----------------------------------------------------------------------===//
//@Register classes
//===----------------------------------------------------------------------===//

def CPURegs : RegisterClass<"Cpu0", [ i32 ], 32, (add(sequence "R%u", 1, 10),
                                                  ZERO, GP, FP, SP, LR, SW)>;

//@Status Registers class
def SR : RegisterClass<"Cpu0", [ i32 ], 32, (add SW)>;
//@Co-processor 0 Registers class
def C0R : RegisterClass<"Cpu0", [ i32 ], 32, (add PC, EPC)>;
