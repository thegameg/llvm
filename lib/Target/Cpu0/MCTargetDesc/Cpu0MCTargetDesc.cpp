//===-- Cpu0MCTargetDesc.cpp - Cpu0 Target Descriptions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Cpu0 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "Cpu0MCAsmInfo.h"
#include "Cpu0MCTargetDesc.h"
#include "Cpu0TargetStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "Cpu0GenInstrInfo.inc"

//#define GET_SUBTARGETINFO_MC_DESC
//#include "Cpu0GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "Cpu0GenRegisterInfo.inc"

static MCInstrInfo *createCpu0MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitCpu0MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createCpu0MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitCpu0MCRegisterInfo(X, Cpu0::LR);
  return X;
}

static MCAsmInfo *createCpu0MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT) {
  MCAsmInfo *MAI = new Cpu0MCAsmInfo(TT);

  unsigned SP = MRI.getDwarfRegNum(Cpu0::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

/*
static MCSubtargetInfo *createCpu0MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  CPU = Cpu0_MC::selectCpu0CPU(TT, CPU);
  return createCpu0MCSubtargetInfoImpl(TT, CPU, FS);
}

static MCCodeGenInfo *createCpu0MCCodeGenInfo(const Triple &TT, Reloc::Model RM,
                                              CodeModel::Model CM,
                                              CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  if (RM == Reloc::Default || CM == CodeModel::JITDefault)
    RM = Reloc::Static;
  X->initMCCodeGenInfo(RM, CM, OL);
  return X;
}

static MCInstPrinter *createCpu0MCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new Cpu0InstPrinter(MAI, MII, MRI);
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    MCAsmBackend &MAB, raw_pwrite_stream &OS,
                                    MCCodeEmitter *Emitter, bool RelaxAll) {
  MCStreamer *S;
  if (!T.isOSNaCl())
    S = createCpu0ELFStreamer(Context, MAB, OS, Emitter, RelaxAll);
  else
    S = createCpu0NaClELFStreamer(Context, MAB, OS, Emitter, RelaxAll);
  return S;
}
*/

static MCTargetStreamer *createCpu0AsmTargetStreamer(MCStreamer &S,
                                                     formatted_raw_ostream &OS,
                                                     MCInstPrinter *InstPrint,
                                                     bool isVerboseAsm) {
  return new Cpu0TargetAsmStreamer(S, OS);
}

/*
static MCTargetStreamer *createCpu0NullTargetStreamer(MCStreamer &S) {
  return new Cpu0TargetStreamer(S);
}
*/

static MCTargetStreamer *
createCpu0ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new Cpu0TargetELFStreamer(S, STI);
}

/*
namespace {

class Cpu0MCInstrAnalysis : public MCInstrAnalysis {
public:
  Cpu0MCInstrAnalysis(const MCInstrInfo *Info) : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    unsigned NumOps = Inst.getNumOperands();
    if (NumOps == 0)
      return false;
    switch (Info->get(Inst.getOpcode()).OpInfo[NumOps - 1].OperandType) {
    case MCOI::OPERAND_UNKNOWN:
    case MCOI::OPERAND_IMMEDIATE:
      // jal, bal ...
      Target = Inst.getOperand(NumOps - 1).getImm();
      return true;
    case MCOI::OPERAND_PCREL:
      // b, j, beq ...
      Target = Addr + Inst.getOperand(NumOps - 1).getImm();
      return true;
    default:
      return false;
    }
  }
};
}

static MCInstrAnalysis *createCpu0MCInstrAnalysis(const MCInstrInfo *Info) {
  return new Cpu0MCInstrAnalysis(Info);
}
*/

extern "C" void LLVMInitializeCpu0TargetMC() {
  Target *T = &TheCpu0Target;

  // Register the MC asm info.
  RegisterMCAsmInfoFn X(*T, createCpu0MCAsmInfo);

  // Register the MC codegen info.
  // TargetRegistry::RegisterMCCodeGenInfo(*T, createCpu0MCCodeGenInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(*T, createCpu0MCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(*T, createCpu0MCRegisterInfo);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(*T, createCpu0AsmTargetStreamer);

  // Register the asm target streamer.
  // TargetRegistry::RegisterAsmTargetStreamer(*T, createCpu0AsmTargetStreamer);

  // TargetRegistry::RegisterNullTargetStreamer(*T,
  // createCpu0NullTargetStreamer);

  // Register the MC subtarget info.
  // TargetRegistry::RegisterMCSubtargetInfo(*T, createCpu0MCSubtargetInfo);

  // Register the MC instruction analyzer.
  // TargetRegistry::RegisterMCInstrAnalysis(*T, createCpu0MCInstrAnalysis);

  TargetRegistry::RegisterObjectTargetStreamer(*T,
                                               createCpu0ObjectTargetStreamer);

  // Register the MC Code Emitter
  /*for (Target *T : {&TheCpu0TargetBig, &TheCpu064Target})
    TargetRegistry::RegisterMCCodeEmitter(*T, createCpu0MCCodeEmitterEB);

  for (Target *T : {&TheCpu0TargetLittle, &TheCpu064elTarget})
    TargetRegistry::RegisterMCCodeEmitter(*T, createCpu0MCCodeEmitterEL);

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(TheCpu0TargetBig,
                                       createCpu0AsmBackendEB32);
  TargetRegistry::RegisterMCAsmBackend(TheCpu0TargetLittle,
                                       createCpu0AsmBackendEL32);
  TargetRegistry::RegisterMCAsmBackend(TheCpu064Target,
                                       createCpu0AsmBackendEB64);
  TargetRegistry::RegisterMCAsmBackend(TheCpu064elTarget,
                                       createCpu0AsmBackendEL64);
  */
}
