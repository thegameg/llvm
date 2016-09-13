//===-- Cpu0AsmBackend.h - Cpu0 Asm Backend  ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Cpu0AsmBackend class.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_LIB_TARGET_CPU0_MCTARGETDESC_CPU0ASMBACKEND_H
#define LLVM_LIB_TARGET_CPU0_MCTARGETDESC_CPU0ASMBACKEND_H

#include "MCTargetDesc/Cpu0FixupKinds.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCAsmBackend.h"

namespace llvm {

class MCAssembler;
struct MCFixupKindInfo;
class Target;
class MCObjectWriter;

class Cpu0AsmBackend : public MCAsmBackend {
  Triple::OSType OSType;

public:
  Cpu0AsmBackend(const Target &T, Triple::OSType OSType)
      : MCAsmBackend(), OSType(OSType) {}

  MCObjectWriter *createObjectWriter(raw_pwrite_stream &OS) const override;

  void applyFixup(const MCFixup &Fixup, char *Data, unsigned DataSize,
                  uint64_t Value, bool IsPCRel) const override {
    // FIXME.
    llvm_unreachable("Not implemented yet.");
  }

  unsigned getNumFixupKinds() const override {
    return Cpu0::NumTargetFixupKinds;
  }

  /// @name Target Relaxation Interfaces
  /// @{

  /// MayNeedRelaxation - Check whether the given instruction may need
  /// relaxation.
  ///
  /// \param Inst - The instruction to test.
  bool mayNeedRelaxation(const MCInst &Inst) const override { return false; }

  /// fixupNeedsRelaxation - Target specific predicate for whether a given
  /// fixup requires the associated instruction to be relaxed.
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    // FIXME.
    llvm_unreachable("RelaxInstruction() unimplemented");
    return false;
  }

  /// RelaxInstruction - Relax the instruction in the given fragment
  /// to the next wider instruction.
  ///
  /// \param Inst - The instruction to relax, which may be the same
  /// as the output.
  /// \param [out] Res On return, the relaxed instruction.
  void relaxInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                        MCInst &Res) const override {
    // FIXME.
    llvm_unreachable("RelaxInstruction() unimplemented");
  }

  /// @}

  bool writeNopData(uint64_t Count, MCObjectWriter *OW) const override;
}; // class Cpu0AsmBackend

} // namespace

#endif
