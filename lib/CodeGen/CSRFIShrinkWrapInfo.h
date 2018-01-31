//===- CSRFIShrinkWrapInfo.h - Track CSR or FI shrink-wrap uses  *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This ShrinkWrapInfo determines blocks that use either callee-saved registers
// or contain any stack operations and marks them as used.
// Since this searches for blocks containing either CSRs or stack operations,
// we can use a single bit to encode this information.
//
// The result should be interpreted in the following way:
// 4:
//   elements:         CSRFI
//   BitVector index:  0
//   BitVector values: 1
//
// Means that %bb.4 uses either CSRs or stack operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CSRFI_SHRINKWRAPINFO_H
#define LLVM_CODEGEN_CSRFI_SHRINKWRAPINFO_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ShrinkWrapper.h"

namespace llvm {

class RegScavenger;

class CSRFIShrinkWrapInfo : public ShrinkWrapInfo {
  /// Track the usage per basic block.
  BitVector Uses;

  using SetOfRegs = SmallSetVector<unsigned, 16>;
  /// Hold callee-saved information.
  mutable SetOfRegs CurrentCSRs;

  RegisterClassInfo RCI;

  /// The machine function we're shrink-wrapping.
  const MachineFunction &MF;

  /// Cache the target's frame setup / destroy opcodes.
  unsigned FrameSetupOpcode = 0;
  unsigned FrameDestroyOpcode = 0;

  /// Determine CSRs and cache the result.
  const SetOfRegs &getCurrentCSRs(RegScavenger *RS) const;
  /// Return true if MI uses any of the resources that we are interested in.
  bool useOrDefCSROrFI(const MachineInstr &MI, RegScavenger *RS) const;

public:
  enum { CSRFIUsedBit = 0, CSRFIShrinkWrapInfoSize };

  /// Run the analysis.
  CSRFIShrinkWrapInfo(const MachineFunction &MF, RegScavenger *RS);

  const BitVector *getUses(unsigned MBBNum) const override;

  const BitVector &getAllUsedElts() const override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_CSRFI_SHRINKWRAPINFO_H
