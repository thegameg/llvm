//===-- X86JaguarLowering.cpp - lower jaguar async calls ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file handles the jaguar llvm intrinsics (as function calls).
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "x86-jaguar"

namespace {
class JaguarLoweringPass : public MachineFunctionPass {
public:
  JaguarLoweringPass() : MachineFunctionPass(ID) {}

  const char *getPassName() const override { return "X86 jaguar lowering"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;

private:
  std::pair<MachineFunction::iterator, MachineBasicBlock::iterator>
  lowerAsyncCall(MachineFunction &MF, MachineFunction::iterator MBB,
                 MachineBasicBlock::iterator it);
};
char JaguarLoweringPass::ID = 0;

bool isTCAsyncCall(MachineInstr &MI) {
  if (!MI.isCall())
    return false;

  auto &MO = MI.getOperand(0);
  if (!MO.isGlobal())
    return false;

  auto *GV = MO.getGlobal();
  auto *F = dyn_cast<Function>(GV);
  if (!F)
    return false;

  if (F->getName() != "llvm.tc_async_call")
    return false;

  return true;
}
}

FunctionPass *llvm::createX86JaguarLowering() {
  return new JaguarLoweringPass();
};

std::pair<MachineFunction::iterator, MachineBasicBlock::iterator>
JaguarLoweringPass::lowerAsyncCall(MachineFunction &MF,
                                   MachineFunction::iterator MFI,
                                   MachineBasicBlock::iterator MBBI) {
  auto DL = MFI->findDebugLoc(MBBI);
  auto &TII = *MF.getSubtarget().getInstrInfo();
  auto &MRI = MF.getRegInfo();
  auto &MBB = *MFI;

  // Registers used across all the function.
  // The arguments array.
  auto ArgsReg = MRI.createVirtualRegister(&X86::GR32_NOSPRegClass);
  // The number of arguments.
  auto NbArgsReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  // Function pointer to call.
  auto FPtrReg = MRI.createVirtualRegister(&X86::GR32RegClass);

  // Pop the arguments. They were pushed in order to call the intrinsics.
  BuildMI(MBB, MBBI, DL, TII.get(X86::POP32r), FPtrReg);
  BuildMI(MBB, MBBI, DL, TII.get(X86::POP32r), NbArgsReg);
  BuildMI(MBB, MBBI, DL, TII.get(X86::POP32r), ArgsReg);

  // Setup a counter.
  // %counter = %nb_args - 1
  auto CounterReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  BuildMI(MBB, MBBI, DL, TII.get(X86::SUB32ri8), CounterReg)
      .addReg(NbArgsReg)
      .addImm(1);

  // Create the necessary basic blocks.
  auto MFII = std::next(MFI);
  // Test: Loop test.
  auto *TestMBB = MF.CreateMachineBasicBlock();
  MF.insert(MFII, TestMBB);
  // Prologue: Prologue preparing the arguments for the call.
  auto *PrologueMBB = MF.CreateMachineBasicBlock();
  MF.insert(MFII, PrologueMBB);
  // Continue: The rest of the instructions after the call.
  auto *ContinueMBB = MF.CreateMachineBasicBlock();
  MF.insert(MFII, ContinueMBB);

  //        MBB
  //         |
  //       Test-------
  //         |        |
  //      Prologue    |
  //         |________|
  //         |
  //     Continue

  TestMBB->addSuccessor(PrologueMBB);
  TestMBB->addSuccessor(ContinueMBB);
  PrologueMBB->addSuccessor(TestMBB);

  // test:
  //   cmp $0, %counter
  //   jl $continue
  BuildMI(TestMBB, DL, TII.get(X86::CMP32ri8)).addReg(CounterReg).addImm(0);
  BuildMI(TestMBB, DL, TII.get(X86::JL_1)).addMBB(ContinueMBB);

  auto TmpReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  // prologue:
  //   mov [%args + %counter * 4], $tmp
  //   push %tmp
  //   dec %counter
  //   jmp $test
  BuildMI(PrologueMBB, DL, TII.get(X86::MOV32rm))
      .addReg(TmpReg, RegState::Define)
      .addReg(ArgsReg)
      .addImm(4)
      .addReg(CounterReg)
      .addImm(0)
      .addReg(0);

  BuildMI(PrologueMBB, DL, TII.get(X86::PUSH32r)).addReg(TmpReg);
  BuildMI(PrologueMBB, DL, TII.get(X86::DEC32r)).addReg(CounterReg);
  BuildMI(PrologueMBB, DL, TII.get(X86::JMP_1)).addMBB(TestMBB);

  // Add the rest of the instructions to the continue block.
  // Skip the first instruction: the call to `@llvm.tc_async_call`.
  ContinueMBB->splice(ContinueMBB->begin(), &MBB, std::next(MBBI), MBB.end());
  ContinueMBB->transferSuccessorsAndUpdatePHIs(&MBB);

  MBB.addSuccessor(TestMBB, BranchProbability::getOne());

  auto ContinueMII = ContinueMBB->begin();
  // continue:
  //   call *%fptr
  //   imul $4, %nbargs, %nbargs
  //   sub $12, %nbargs
  //   add %nbargs, %esp ------------------ esp += 4 * %nbargs - 12
  //   rest_of_the_instructions
  BuildMI(*ContinueMBB, ContinueMII, DL, TII.get(X86::CALL32r))
      .addReg(FPtrReg)
      .addReg(X86::EAX, RegState::ImplicitDefine)
      .addRegMask(MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(
          MF, CallingConv::C));

  auto MulResultReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  BuildMI(*ContinueMBB, ContinueMII, DL, TII.get(X86::IMUL32rri), MulResultReg)
      .addReg(NbArgsReg)
      .addImm(4);

  auto SubResultReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  BuildMI(*ContinueMBB, ContinueMII, DL, TII.get(X86::SUB32ri8), SubResultReg)
      .addReg(MulResultReg)
      .addImm(12);

  auto ESPResultReg = MRI.createVirtualRegister(&X86::GR32RegClass);
  BuildMI(*ContinueMBB, ContinueMII, DL, TII.get(X86::ADD32rr), ESPResultReg)
      .addReg(SubResultReg)
      .addReg(X86::ESP);

  BuildMI(*ContinueMBB, ContinueMII, DL, TII.get(X86::COPY), X86::ESP)
      .addReg(ESPResultReg);

  MBB.back().eraseFromParent();

  return {ContinueMBB->getIterator(), ContinueMII};
}

bool JaguarLoweringPass::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  for (auto MFI = MF.begin(); MFI != MF.end(); ++MFI) {
    for (auto MBBI = MFI->begin(); MBBI != MFI->end(); ++MBBI) {
      auto &MI = *MBBI;
      if (isTCAsyncCall(MI)) {
        std::tie(MFI, MBBI) = lowerAsyncCall(MF, MFI, MBBI);
        Changed |= true;
      }
    }
  }

  return Changed;
}
