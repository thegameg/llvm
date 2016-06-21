//===-- Cpu0AsmBackend.cpp - Cpu0 Asm Backend  ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cpu0AsmBackend class.
//
//===----------------------------------------------------------------------===//
//

#include "MCTargetDesc/Cpu0AsmBackend.h"
#include "MCTargetDesc/Cpu0MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class Cpu0ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  Cpu0ELFObjectWriter(bool IsELF64, uint8_t OSABI, uint16_t EMachine)
      : MCELFObjectTargetWriter(IsELF64, OSABI, EMachine, IsELF64) {}

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override {
    return ELF::R_CPU0_NONE;
  }
};
}

MCObjectWriter *
Cpu0AsmBackend::createObjectWriter(raw_pwrite_stream &OS) const {
  auto *MOTW = new Cpu0ELFObjectWriter(
      false, MCELFObjectTargetWriter::getOSABI(OSType), ELF::EM_CPU0);
  return createELFObjectWriter(MOTW, OS, /* isLittle */ true);
}

/// WriteNopData - Write an (optimal) nop sequence of Count bytes
/// to the given output. If the target cannot generate such a sequence,
/// it should return an error.
///
/// \return - True on success.
bool Cpu0AsmBackend::writeNopData(uint64_t Count, MCObjectWriter *OW) const {
  // If the count is not 4-byte aligned, we must be writing data into the text
  // section (otherwise we have unaligned instructions, and thus have far
  // bigger problems), so just write zeros instead.
  OW->WriteZeros(Count);
  return true;
}

// MCAsmBackend
MCAsmBackend *llvm::createCpu0AsmBackend(const Target &T,
                                         const MCRegisterInfo &MRI,
                                         const Triple &TT, StringRef CPU,
                                         const MCTargetOptions &Options) {
  return new Cpu0AsmBackend(T, TT.getOS());
}
