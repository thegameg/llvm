//=- AArch64MachineFunctionInfo.h - AArch64 machine function info -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares AArch64-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64MACHINEFUNCTIONINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include <cassert>

namespace llvm {

class MachineInstr;
class MachineOperand;

/// This contains register pairs computed for callee-save saves / restores.
struct RegPairInfo {
  unsigned Reg1 = AArch64::NoRegister;
  unsigned Reg2 = AArch64::NoRegister;
  int FrameIdx;
  int Offset;
  bool IsGPR;

  RegPairInfo() = default;

  bool isPaired() const { return Reg2 != llvm::AArch64::NoRegister; }
};

/// AArch64FunctionInfo - This class is derived from MachineFunctionInfo and
/// contains private AArch64-specific information for each MachineFunction.
class AArch64FunctionInfo final : public MachineFunctionInfo {
  /// Number of bytes of arguments this function has on the stack. If the callee
  /// is expected to restore the argument stack this should be a multiple of 16,
  /// all usable during a tail call.
  ///
  /// The alternative would forbid tail call optimisation in some cases: if we
  /// want to transfer control from a function with 8-bytes of stack-argument
  /// space to a function with 16-bytes then misalignment of this value would
  /// make a stack adjustment necessary, which could not be undone by the
  /// callee.
  unsigned BytesInStackArgArea = 0;

  /// The number of bytes to restore to deallocate space for incoming
  /// arguments. Canonically 0 in the C calling convention, but non-zero when
  /// callee is expected to pop the args.
  unsigned ArgumentStackToRestore = 0;

  /// HasStackFrame - True if this function has a stack frame. Set by
  /// determineCalleeSaves().
  // FIXME: ShrinkWrap2: This should not be set in determineCalleeSaves...
  bool HasStackFrame = false;

  /// \brief Amount of stack frame size, not including callee-saved registers.
  unsigned LocalStackSize;

  /// \brief Amount of stack frame size used for saving callee-saved registers.
  unsigned CalleeSavedStackSize;

  /// \brief Number of TLS accesses using the special (combinable)
  /// _TLS_MODULE_BASE_ symbol.
  unsigned NumLocalDynamicTLSAccesses = 0;

  /// \brief FrameIndex for start of varargs area for arguments passed on the
  /// stack.
  int VarArgsStackIndex = 0;

  /// \brief FrameIndex for start of varargs area for arguments passed in
  /// general purpose registers.
  int VarArgsGPRIndex = 0;

  /// \brief Size of the varargs area for arguments passed in general purpose
  /// registers.
  unsigned VarArgsGPRSize = 0;

  /// \brief FrameIndex for start of varargs area for arguments passed in
  /// floating-point registers.
  int VarArgsFPRIndex = 0;

  /// \brief Size of the varargs area for arguments passed in floating-point
  /// registers.
  unsigned VarArgsFPRSize = 0;

  /// True if this function has a subset of CSRs that is handled explicitly via
  /// copies.
  bool IsSplitCSR = false;

  /// True when the stack gets realigned dynamically because the size of stack
  /// frame is unknown at compile time. e.g., in case of VLAs.
  bool StackRealigned = false;

  /// True when the callee-save stack area has unused gaps that may be used for
  /// other stack allocations.
  bool CalleeSaveStackHasFreeSpace = false;

  // FIXME: ShrinkWrap2: This should be replaced with MFI.Objects.
  /// Register pairs computed for CSR save / restore.
  SmallVector<RegPairInfo, 8> RegPairs;

  // FIXME: ShrinkWrap2: The offsets that probably need to be fixed are
  // collected during spillCalleeSavedRegisters but need to be fixed during
  // emitPrologue.
  /// Machine operands representing SP-related offsets to CSRs, that need to be
  /// fixed if local stack allocation happens afterwards.
  SmallVector<MachineOperand*, 8> CSROffsetsToFix;

  // FIXME: ShrinkWrap2: Find somewhere else to put this.
  BitVector ShrinkWrapPrologue;
  BitVector ShrinkWrapEpilogue;
  BitVector EntryCSRs;

public:
  AArch64FunctionInfo() = default;

  explicit AArch64FunctionInfo(MachineFunction &MF) {
    (void)MF;
  }

  unsigned getBytesInStackArgArea() const { return BytesInStackArgArea; }
  void setBytesInStackArgArea(unsigned bytes) { BytesInStackArgArea = bytes; }

  unsigned getArgumentStackToRestore() const { return ArgumentStackToRestore; }
  void setArgumentStackToRestore(unsigned bytes) {
    ArgumentStackToRestore = bytes;
  }

  bool hasStackFrame() const { return HasStackFrame; }
  void setHasStackFrame(bool s) { HasStackFrame = s; }

  bool isStackRealigned() const { return StackRealigned; }
  void setStackRealigned(bool s) { StackRealigned = s; }

  bool hasCalleeSaveStackFreeSpace() const {
    return CalleeSaveStackHasFreeSpace;
  }
  void setCalleeSaveStackHasFreeSpace(bool s) {
    CalleeSaveStackHasFreeSpace = s;
  }

  SmallVectorImpl<RegPairInfo> &getRegPairs() { return RegPairs; }
  SmallVectorImpl<MachineOperand *> &getCSROffsetsToFix() {
    return CSROffsetsToFix;
  }

  bool isSplitCSR() const { return IsSplitCSR; }
  void setIsSplitCSR(bool s) { IsSplitCSR = s; }

  void setLocalStackSize(unsigned Size) { LocalStackSize = Size; }
  unsigned getLocalStackSize() const { return LocalStackSize; }

  void setCalleeSavedStackSize(unsigned Size) { CalleeSavedStackSize = Size; }
  unsigned getCalleeSavedStackSize() const { return CalleeSavedStackSize; }

  void incNumLocalDynamicTLSAccesses() { ++NumLocalDynamicTLSAccesses; }
  unsigned getNumLocalDynamicTLSAccesses() const {
    return NumLocalDynamicTLSAccesses;
  }

  int getVarArgsStackIndex() const { return VarArgsStackIndex; }
  void setVarArgsStackIndex(int Index) { VarArgsStackIndex = Index; }

  int getVarArgsGPRIndex() const { return VarArgsGPRIndex; }
  void setVarArgsGPRIndex(int Index) { VarArgsGPRIndex = Index; }

  unsigned getVarArgsGPRSize() const { return VarArgsGPRSize; }
  void setVarArgsGPRSize(unsigned Size) { VarArgsGPRSize = Size; }

  int getVarArgsFPRIndex() const { return VarArgsFPRIndex; }
  void setVarArgsFPRIndex(int Index) { VarArgsFPRIndex = Index; }

  unsigned getVarArgsFPRSize() const { return VarArgsFPRSize; }
  void setVarArgsFPRSize(unsigned Size) { VarArgsFPRSize = Size; }

  using SetOfInstructions = SmallPtrSet<const MachineInstr *, 16>;

  const SetOfInstructions &getLOHRelated() const { return LOHRelated; }

  // Shortcuts for LOH related types.
  class MILOHDirective {
    MCLOHType Kind;

    /// Arguments of this directive. Order matters.
    SmallVector<const MachineInstr *, 3> Args;

  public:
    using LOHArgs = ArrayRef<const MachineInstr *>;

    MILOHDirective(MCLOHType Kind, LOHArgs Args)
        : Kind(Kind), Args(Args.begin(), Args.end()) {
      assert(isValidMCLOHType(Kind) && "Invalid LOH directive type!");
    }

    MCLOHType getKind() const { return Kind; }
    LOHArgs getArgs() const { return Args; }
  };

  using MILOHArgs = MILOHDirective::LOHArgs;
  using MILOHContainer = SmallVector<MILOHDirective, 32>;

  const MILOHContainer &getLOHContainer() const { return LOHContainerSet; }

  /// Add a LOH directive of this @p Kind and this @p Args.
  void addLOHDirective(MCLOHType Kind, MILOHArgs Args) {
    LOHContainerSet.push_back(MILOHDirective(Kind, Args));
    LOHRelated.insert(Args.begin(), Args.end());
  }

  BitVector &getShrinkWrapPrologues() { return ShrinkWrapPrologue; }
  BitVector &getShrinkWrapEpilogues() { return ShrinkWrapEpilogue; }

  BitVector &getEntryCSRs() { return EntryCSRs; }
  const BitVector &getEntryCSRs() const { return EntryCSRs; }

  bool isShrinkWrapPrologue(unsigned MBBNum) const {
    if (ShrinkWrapPrologue.empty())
      return false;
    return ShrinkWrapPrologue.test(MBBNum);
  }
  bool isShrinkWrapEpilogue(unsigned MBBNum) const {
    if (ShrinkWrapEpilogue.empty())
      return false;
    return ShrinkWrapEpilogue.test(MBBNum);
  }

private:
  // Hold the lists of LOHs.
  MILOHContainer LOHContainerSet;
  SetOfInstructions LOHRelated;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_AARCH64MACHINEFUNCTIONINFO_H
