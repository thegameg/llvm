//===- llvm/CodeGen/ShrinkWrapper.h - Shrink Wrapping Utility ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This class is the main utility to provide shrink-wrapping properties to any
// kind of attributes. This is used to do callee-saved registers and stack
// shrink-wrapping. The algorithm is based on "Minimizing Register Usage Penalty
// at Procedure Calls - Fred C. Chow" [1], with the usage of SCCs to exclude
// loops and provide a linear pass instead of a complete dataflow analysis.
// FIXME: ShrinkWrap2: Random thoughts:
// - r193749 removed an old pass that was an implementation of [1].
// - Cost model: use MachineBlockFrequency and some instruction cost model?
// - Split critical edges on demand?
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHRINKWRAP_H
#define LLVM_CODEGEN_SHRINKWRAP_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"

#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// Iterator for successors / predecessors. This is here to work with
/// SmallVector and std::vector at the same time.
typedef const MachineBasicBlock *const *MBBIterator;

class ShrinkWrapper {
  typedef BitVector MBBSet;
  /// Result type used to store results / uses. The target decides the meaning
  /// of the bits.
  typedef BitVector TargetResultSet;
  // Idx = MBB.getNumber()
  typedef SmallVector<TargetResultSet, 8> BBResultSetMap;
  typedef DenseMap<unsigned, TargetResultSet> SparseBBResultSetMap;

  // FIXME: ShrinkWrap2: Explain anticipated / available and how the properties
  // are used.
  struct SWAttributes {
    /// Is the element anticipated at the end of this basic block?
    TargetResultSet ANTOUT;
    /// Is the element anticipated at the beginning of this basic block?
    TargetResultSet ANTIN;
    /// Is the element available at the beginning of this basic block?
    TargetResultSet AVIN;
    /// Is the element available at the end of this basic block?
    TargetResultSet AVOUT;

    SWAttributes(const ShrinkWrapInfo &SWI) {
      unsigned Max = SWI.getNumResultBits();
      for (TargetResultSet *Maps : {&ANTOUT, &ANTIN, &AVIN, &AVOUT})
        (*Maps).resize(Max);
    }
  };

  // Idx = MBB.getNumber()
  typedef SmallVector<SWAttributes, 8> AttributeMap;

  /// An SCC that was discovered through the scc_iterator on the function.
  /// This is used in order to detect loops, reducible *AND* irreducible.
  struct SCCLoop {
    typedef SmallVector<const MachineBasicBlock *, 4> MBBVector;
    /// The successors of the SCC. There are blocks outside the SCC.
    SetVector<const MachineBasicBlock *, MBBVector> Successors;
    iterator_range<MBBIterator> successors() const {
      return {&*Successors.begin(), &*Successors.end()};
    }
    /// The predecessors of the SCC. There are blocks outside the SCC.
    SetVector<const MachineBasicBlock *, MBBVector> Predecessors;
    iterator_range<MBBIterator> predecessors() const {
      return {&*Predecessors.begin(), &*Predecessors.end()};
    }
    /// The SCC number. This number is the smallest basic block number int he
    /// SCC. This is used when we replace basic blocks with SCCs for loops.
    unsigned Number;
    unsigned getNumber() const { return Number; }
    /// The number of blocks the SCC contains.
    unsigned Size;
    unsigned getSize() const { return Size; }
  };

  /// Wrapper around scc_iterator that collects SCCs that are loops, computes
  /// their successor / predecessor and assigns an unique number based on the
  /// basic blocks it contains.
  struct SCCLoopInfo {
    /// Own the SCCs.
    SmallVector<SCCLoop, 4> SCCs;
    /// Map a basic block number to an SCCLoop number. The SCCLoop number is
    /// the position in the `SCCs` vector, and it is differrent from the
    /// SCC::Number attribute, which is the smallest basic block number in the
    /// SCC.
    DenseMap<unsigned, unsigned> MBBToSCC;

    /// Initialize the successors / predecessors of the SCCLoops.
    SCCLoopInfo(const MachineFunction &MF);
    /// Get the SCCLoop for a designated basic block number. If there is no
    /// SCCLoop associated, return `nullptr`.
    SCCLoop *getSCCLoopFor(unsigned MBBNum) {
      auto It = MBBToSCC.find(MBBNum);
      if (It == MBBToSCC.end())
        return nullptr;
      return &SCCs[It->second];
    }
    const SCCLoop *getSCCLoopFor(unsigned MBBNum) const {
      return const_cast<SCCLoopInfo *>(this)->getSCCLoopFor(MBBNum);
    }
  };

  /// The MachineFunction we're working on.
  const MachineFunction &MF;

  /// Target-found uses.
  SparseBBResultSetMap Uses;

  /// Blocks that never return.
  MBBSet NoReturnBlocks;

  /// Target-specific shrink-wrap information.
  std::unique_ptr<ShrinkWrapInfo> SWI;

  /// The replacement for the MachineLoopInfo, that handling irreducible loops
  /// as well.
  SCCLoopInfo SI;

  /// Final results.
  SparseBBResultSetMap Saves;
  SparseBBResultSetMap Restores;

public:
  TargetResultSet Elts;
  private:

  /// Get the block number or the SCCLoop's number.
  unsigned blockNumber(unsigned MBBNum) const;

  /// Get the block successors or the SCCLoop exit blocks.
  iterator_range<MBBIterator> blockSuccessors(unsigned MBBNum) const;

  /// Get the block predecessors or the SCCLoop's predecessors.
  iterator_range<MBBIterator> blockPredecessors(unsigned MBBNum) const;

  /// Populate the attribute maps with trivial properties from the used
  /// elements.
  void populateAttributes(AttributeMap &Attrs) const;

  /// Compute the attributes for one element.
  // FIXME: ShrinkWrap2: Don't do this per element.
  void computeAttributes(unsigned Elt, AttributeMap &Attrs) const;
  /// Save the results for this particular element.
  // FIXME: ShrinkWrap2: Don't do this per element.
  bool gatherAttributesResults(unsigned Elt, AttributeMap &Attrs);
  /// Dump the contents of the attributes.
  // FIXME: ShrinkWrap2: Don't do this per element.
  void dumpAttributes(unsigned Elt, AttributeMap &Attrs) const;

  /// Determine uses based on SWI.
  void determineUses();

  /// Remove uses and fill NoReturnBlocks with the blocks that we know are not
  /// going to return from the function.
  void removeUsesOnNoReturnPaths();
  void dumpUses() const;

  /// Mark all the basic blocks around the loop (pred, succ) as used,
  /// if there is an usage of a CSR inside a loop. We want to avoid any save /
  /// restore operations in a loop.
  void markUsesOutsideLoops();

  /// * Verify if the results are better than obvious results, like:
  ///   * CSR used in a single MBB: only one save and one restore.
  /// * Remove empty entries from the Saves / Restores maps.
  // FIXME: ShrinkWrap2: This shouldn't happen, we better fix the algorithm
  // first.
  void postProcessResults(const SparseBBResultSetMap &OldUses);

  /// Verify save / restore points by walking the CFG.
  /// This asserts if anything went wrong.
  // FIXME: ShrinkWrap2: Should we add a special flag for this?
  // FIXME: ShrinkWrap2: Expensive checks?
  void verifySavesRestores() const;

  /// Dump the final shrink-wrapping results.
  void dumpResults() const;

public:
  ShrinkWrapper(const MachineFunction &MF);
  bool hasUses() const { return !Uses.empty(); }
  const SparseBBResultSetMap &getSaves() { return Saves; }
  const SparseBBResultSetMap &getRestores() { return Restores; }

  virtual ~ShrinkWrapper() = default;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SHRINKWRAP_H
