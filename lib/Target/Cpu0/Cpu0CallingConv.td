//===-- Cpu0CallingConv.td - Calling Conventions for Cpu0 --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for Cpu0 architecture.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Cpu0 Calling Convention
//===----------------------------------------------------------------------===//

def CC_Cpu0 : CallingConv<[
  CCIfType<[ i1, i8, i16 ], CCPromoteToType<i32>>,
  CCIfType<[ i32 ], CCAssignToReg<[ R4, R5 ]>>
]>;

//===----------------------------------------------------------------------===//
// Return Value Calling Convention
//===----------------------------------------------------------------------===//

def RetCC_Cpu0 : CallingConv<[ CCIfType<[ i32 ], CCAssignToReg<[ R2, R3 ]>> ]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers.
//===----------------------------------------------------------------------===//

def CSR : CalleeSavedRegs<(add R9, R10)>;
