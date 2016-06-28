//===-- Cpu0ISelLowering.cpp - Cpu0 DAG Lowering Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Cpu0 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "Cpu0ISelLowering.h"
#include "Cpu0TargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#include "Cpu0GenRegisterInfo.inc"

#include "Cpu0GenCallingConv.inc"

Cpu0TargetLowering::Cpu0TargetLowering(const Cpu0TargetMachine &TM,
                                       const Cpu0Subtarget &STI)
    : TargetLowering(TM) {
  // Add CPURegs class as i32 registers.
  addRegisterClass(MVT::i32, &Cpu0::CPURegsRegClass);
  computeRegisterProperties(STI.getRegisterInfo());
}

//===----------------------------------------------------------------------===//
//             Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//

/// LowerFormalArguments - transform physical registers into virtual registers
/// and generate load operations for arguments places on the stack.

const char *Cpu0TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((Cpu0ISD::NodeType)Opcode) {
  case Cpu0ISD::FIRST_NUMBER:
    break;
#define CASE(X)                                                                \
  case Cpu0ISD::X:                                                             \
    return #X;
    CASE(JmpLink)
    CASE(Ret)
#undef CASE
  }

  return nullptr;
}

SDValue Cpu0TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  assert(!IsVarArg && "Variable arguments not supported.");

  auto &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Cpu0);

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    MVT RegVT = VA.getLocVT();
    const TargetRegisterClass *RC = getRegClassFor(RegVT);
    unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegVT);
    InVals.push_back(ArgValue);
  }

  return Chain;
}

SDValue
Cpu0TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool isVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_Cpu0);

  SDValue Flag;
  SmallVector<SDValue, 1> RetOps;

  assert(RVLocs.size() == 1 && "More than 1 return value not supported.");

  // Copy the result values into the output registers.
  CCValAssign &VA = RVLocs[0];
  SDValue ValToCopy = OutVals[0];

  RetOps.push_back(
      DAG.getCopyToReg(Chain, DL, VA.getLocReg(), ValToCopy, Flag));
  RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));

  // FIXME: Flag?
#if 0
  Flag = Chain.getValue(1);
  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);
#endif

  return DAG.getNode(Cpu0ISD::Ret, DL, MVT::Other, RetOps);
}
