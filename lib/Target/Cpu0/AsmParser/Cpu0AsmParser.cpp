//===-- Cpu0AsmParser.cpp - Parse Cpu0 assembly to MCInst instructions ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Cpu0TargetMachine.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#include "Cpu0GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "Cpu0GenInstrInfo.inc"

namespace {
class Cpu0AsmParser : public MCTargetAsmParser {

public:
  Cpu0AsmParser(const MCSubtargetInfo &STI, MCAsmParser &P,
                const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI) {}

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override {
    // FIXME: Implement.
    return true;
  }

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override {
    // FIXME: Implement.
    return true;
  }

  bool ParseDirective(AsmToken DirectiveID) override {
    // FIXME: Implement.
    return true;
  }

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override {
    // FIXME: Implement.
    return true;
  }

#define GET_ASSEMBLER_HEADER
#include "Cpu0GenAsmMatcher.inc"
};

class Cpu0Operand : public MCParsedAsmOperand {
public:
  void addRegOperands(MCInst &Inst, unsigned N) const {
    // FIXME : Implement.
    llvm_unreachable("Not implemented yet");
    // Inst.addOperand(MCOperand::createReg(getReg()));
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    // FIXME : Implement.
    llvm_unreachable("Not implemented yet");
  }

  StringRef getToken() const {
    // FIXME : Implement.
    llvm_unreachable("Not implemented yet");
    return {};
  }
};
}

extern "C" void LLVMInitializeCpu0AsmParser() {
  RegisterMCAsmParser<Cpu0AsmParser> X(TheCpu0Target);
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "Cpu0GenAsmMatcher.inc"
