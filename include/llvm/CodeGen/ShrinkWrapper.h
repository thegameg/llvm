//===- llvm/CodeGen/ShrinkWrapper.h - Shrink Wrapping Utility ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the main interface to use different shrink-wrapping algorithms with
// different kinds of inputs.
//
// For that, the interface is split in two parts:
//
// * ShrinkWrapInfo: it is an interface that determines the "uses" to be used
// by the shrink-wrapping algorithm.
//
// * ShrinkWrapper: it is an interface for the shrink-wrapping algorithm. It
// makes it easy to switch between the current one (dominator-based) and other
// ones like Fred Chow's.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHRINKWRAPPER_H
#define LLVM_CODEGEN_SHRINKWRAPPER_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>

namespace llvm {

class MachineFunction;
class MachineBasicBlock;
class MachineBlockFrequencyInfo;
class MachineOptimizationRemarkEmitter;
class raw_ostream;

/// Information about the requirements on shrink-wrapping. This should describe
/// what does "used" mean, and it should be the main interface to work with the
/// targets and other shrink-wrappable inputs.
/// It is meant to be sub-classed.
class ShrinkWrapInfo {
  /// Example 1: Callee saved registers:
  ///  0:
  ///    elements:         r0 r1 r2 r3 r4 r5 r6 r7 r8 r9
  ///    BitVector index:  0  1  2  3  4  5  6  7  8  9
  ///    BitVector values: 1  0  0  0  0  0  0  0  0  1
  ///  This means that r0 and r9 are used in %bb.0.
  ///
  /// Example 2: Stack + CSRs:
  ///  0:
  ///    elements:         Stack r0 r1
  ///    BitVector index:  0     1  2
  ///    BitVector values: 1     0  1
  ///  4:
  ///    elements:         Stack r0 r1
  ///    BitVector index:  0     1  2
  ///    BitVector values: 0     1  0
  ///  This means that r0 is used in %bb.4 and the Stack and r1 are used in
  ///  %bb.0.
public:
  ShrinkWrapInfo() = default;

  /// Get the number of elements that are tracked.
  unsigned getNumElts() const { return getAllUsedElts().size(); }

  /// Return a BitVector containing all the used elements.
  virtual const BitVector &getAllUsedElts() const = 0;

  /// Get the elements that are used for a particular basic block. The result is
  /// `nullptr` if there are no uses.
  virtual const BitVector *getUses(unsigned MBBNum) const = 0;

  virtual ~ShrinkWrapInfo() = default;
};

/// The interface of a shrink-wrapping algorithm. This class should implement a
/// shrink-wrapping algorithm. It should get the "uses" from the ShrinkWrapInfo
/// object and return the save / restore points in the Saves Restores maps. This
/// allows us to switch shrink-wrapping implementations easily.
class ShrinkWrapper {
public:
  /// The result is a map that maps a MachineBasicBlock number to a BitVector
  /// containing the elements that need to be saved / restored.
  ///
  /// Example 1: Saves:
  /// 0:
  ///   elements:         r0 r1 r2 r3 r4 r5 r6 r7 r8 r9
  ///   BitVector index:  0  1  2  3  4  5  6  7  8  9
  ///   BitVector values: 1  0  0  0  0  0  0  0  0  1
  /// This means that r0 and r9 need to be saved in %bb.0.
  ///
  /// Example 2: Saves:
  /// 0:
  ///   elements:         Stack r0 r1
  ///   BitVector index:  0     1  2
  ///   BitVector values: 1     0  1
  /// 4:
  ///   elements:         Stack r0 r1
  ///   BitVector index:  0     1  2
  ///   BitVector values: 0     1  0
  /// This means that we have to set up the stack and save r1 in %bb.0, and
  /// save r0 in %bb.4
  typedef DenseMap<unsigned, BitVector> BBResultSetMap;

protected:
  /// Final results.
  BBResultSetMap SavesResult;
  BBResultSetMap RestoresResult;

public:
  /// Run the shrink-wrapper on the function. If there are no uses, there will
  /// be no saves / restores.
  ShrinkWrapper() = default;

  virtual ~ShrinkWrapper() = default;

  /// Get the final results.
  /// If any of the expected elements doesn't show up at all in the Save /
  /// Restore map, it means there is no better placement for saving / restoring
  /// that element than the entry / return blocks. The caller has the liberty
  /// to decide what to do with these elements.
  const BBResultSetMap &getSaves() { return SavesResult; }
  const BBResultSetMap &getRestores() { return RestoresResult; }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SHRINKWRAPPER_H
