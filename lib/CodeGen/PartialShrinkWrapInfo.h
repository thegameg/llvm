//===- PartialShrinkWrapInfo.h - Track CSRs separately for SW  *- C++ ---*-===//
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
// The difference from CSRFIShrinkWrapInfo, is that this utility tracks
// registers separately. While registers are tracked separately, any register
// use will also set the stack as used, since we want to be sure that the stack
// setup happens before *all* register saves. For that, we use 1 bit per CSR,
// and 1 bit for the stack.
//
// The result should be interpreted in the following way:
// 4:
//   elements:         Stack r0 r1 r2
//   BitVector index:  0     1  2  3
//   BitVector values: 1     0  0  1
//
// Means that %bb.4 uses the stack and the CSR r2.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PARTIAL_SHRINKWRAPINFO_H
#define LLVM_CODEGEN_PARTIAL_SHRINKWRAPINFO_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ShrinkWrapper.h"

namespace llvm {

class RegScavenger;

class PartialShrinkWrapInfo : public ShrinkWrapInfo {
  /// Track the usage per basic block.
  SmallVector<BitVector, 4> Uses;

  /// All the used elements.
  BitVector AllUsedElts;

  /// Cache the target's frame setup / destroy opcodes.
  unsigned FrameSetupOpcode = 0;
  unsigned FrameDestroyOpcode = 0;

public:
  /// Run the analysis.
  PartialShrinkWrapInfo(const MachineFunction &MF, RegScavenger *RS);

  enum UseBits { StackBit = 0, FirstCSRBit = 1 };

  const BitVector *getUses(unsigned MBBNum) const override;

  const BitVector &getAllUsedElts() const override { return AllUsedElts; }

};

} // end namespace llvm

#endif // LLVM_CODEGEN_PARTIAL_SHRINKWRAPINFO_H
