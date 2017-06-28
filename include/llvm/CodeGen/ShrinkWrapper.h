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
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHRINKWRAP_H
#define LLVM_CODEGEN_SHRINKWRAP_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/PostOrderIterator.h"

#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class MachineBlockFrequencyInfo;
class MachineOptimizationRemarkEmitter;

/// Information about the requirements on shrink-wrapping. This should describe
/// what does "used" mean, and it should be the main interface to work with the
/// targets and other shrink-wrappable inputs.
class ShrinkWrapInfo {
protected:
  /// Track all the uses per basic block.
  SmallVector<BitVector, 8> Uses;

  /// The machine function we're shrink-wrapping.
  const MachineFunction &MF;

  /// Generic code to determine callee saved register uses. This checks for
  /// regmasks, and tracks all the register units.
  /// If there is an use on a terminator, the successors will also be marked as
  /// used.
  void determineCSRUses();

public:
  ShrinkWrapInfo(const MachineFunction &MF)
      : Uses(MF.getNumBlockIDs()), MF(MF) {}
  /// Get the number of results we want per block. i.e. number of registers in
  /// the target.
  virtual unsigned getNumResultBits() const { return 0; }

  /// Get the elements that are used for a particular basic block. The result is
  /// `nullptr` if there are no uses.
  virtual const BitVector *getUses(unsigned MBBNum) const;

  /// Provide a way to print elements. Debug only.
  virtual raw_ostream &printElt(unsigned Elt, raw_ostream &OS) const {
    OS << Elt;
    return OS;
  };

  virtual ~ShrinkWrapInfo() = default;
};

/// Iterator for successors / predecessors. This is here to work with
/// SmallVector and std::vector at the same time.
// FIXME: ShrinkWrap2: Use ArrayRef?
typedef const MachineBasicBlock *const *MBBIterator;

class ShrinkWrapper {
  typedef BitVector MBBSet;
  /// Result type used to store results / uses. The target decides the meaning
  /// of the bits.
  typedef BitVector TargetResultSet;
  // Idx = MBB.getNumber()
  typedef SmallVector<TargetResultSet, 8> BBResultSetMap;
  typedef DenseMap<unsigned, TargetResultSet> SparseBBResultSetMap;

  /// The shrink-wrapping analysis is based on two properties:
  /// * Anticipation:
  /// The use of a register is ancicipated at a given point if a use of the
  /// register will be encountered in all possible execution paths leading from
  /// that point.

  /// * Availability:
  /// The use of a register is available at a given point if a use of the
  /// register has been encountered in all possible execution paths that lead to
  /// that point.

  /// Both attributes are propagated at the beginning and at the end of a block
  /// (which could be an SCC, or a basic block).
  struct SWAttributes {
    /// Is the element anticipated at the beginning of this block?
    TargetResultSet ANTIN;
    /// Is the element available at the end of this block?
    TargetResultSet AVOUT;

    /// Resize all the sets.
    SWAttributes(const ShrinkWrapInfo &SWI) {
      unsigned Max = SWI.getNumResultBits();
      for (TargetResultSet *BV : {&ANTIN, &AVOUT})
        (*BV).resize(Max);
    }
  };

  // Idx = MBB.getNumber()
  typedef SmallVector<SWAttributes, 4> AttributeMap;

  /// An SCC that was discovered through the scc_iterator on the function.
  /// This is used in order to detect loops, reducible *AND* irreducible.
  struct SCCLoop {
    typedef SmallVector<const MachineBasicBlock *, 4> MBBVector;
    /// The successors of the SCC. These are blocks outside the SCC.
    SetVector<const MachineBasicBlock *, MBBVector> Successors;
    iterator_range<MBBIterator> successors() const {
      return {&*Successors.begin(), &*Successors.end()};
    }
    /// The predecessors of the SCC. These are blocks outside the SCC.
    SetVector<const MachineBasicBlock *, MBBVector> Predecessors;
    iterator_range<MBBIterator> predecessors() const {
      return {&*Predecessors.begin(), &*Predecessors.end()};
    }
    /// This number is the number of the first MBB in the SCC.
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
    /// SCCLoop::Number attribute, which is the first basic block's number in
    /// the SCC.
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
  // FIXME: ShrinkWrap2: Use the one from ShrinkWrapInfo, but detecting critical
  // edges may need to modify it.
  BBResultSetMap Uses;
  BBResultSetMap OriginalUses;

  // FIXME: ShrinkWrap2: Is this the correct place to compute this?
  /// Blocks that never return.
  MBBSet NoReturnBlocks;

  /// Target-specific shrink-wrap information.
  std::unique_ptr<ShrinkWrapInfo> SWI;

  /// The replacement for the MachineLoopInfo, that handles irreducible loops
  /// as well.
  SCCLoopInfo SI;

  /// Final results.
  SparseBBResultSetMap Saves;
  SparseBBResultSetMap Restores;

  /// Number of times the attributes have been recomputed because of critical
  /// edges.
  unsigned AttributesRecomputed = 0;

  /// All the elements encountered so far.
  TargetResultSet AllElts;

  /// The CFG we're working on is no longer composed of basic blocks. It's
  /// basically the CFG of SCCs, and we're using numbers to identify nodes. A
  /// simple basic block's number is MBB->getNumber(), and a SCC that is a
  /// loop gets the number of the first basic block encountered. For that,
  /// we're using the following functions to traverse our CFG.

  /// Get the block number or the SCCLoop's number.
  unsigned blockNumber(unsigned MBBNum) const;
  /// Get the block successors or the SCCLoop exit blocks.
  iterator_range<MBBIterator> blockSuccessors(unsigned MBBNum) const;
  /// Get the block predecessors or the SCCLoop's predecessors.
  iterator_range<MBBIterator> blockPredecessors(unsigned MBBNum) const;

  /// Anticipability
  // If there is an use of this on *all* the paths starting from
  // this basic block, the element is anticipated at the end of this
  // block.
  // (propagate the IN attribute of successors to possibly merge saves)
  //           -
  //          | *false*             if no successor.
  // ANTOUT = |
  //          | && ANTIN(succ[i])   otherwise.
  //
  bool ANTOUT(const AttributeMap &Attrs, unsigned MBBNum, unsigned Elt) const {
    auto Successors = blockSuccessors(MBBNum);
    if (Successors.begin() == Successors.end())
      return false;
    return all_of(Successors, [&](const MachineBasicBlock *S) {
      return Attrs[blockNumber(S->getNumber())].ANTIN.test(Elt);
    });
  }

  /// Availability
  // If there is an use of this on *all* the paths arriving in this block,
  // then the element is available in this block (propagate the out attribute
  // of predecessors to possibly merge restores).
  //         -
  //        | *false*             if no predecessor.
  // AVIN = |
  //        | && AVOUT(pred[i])   otherwise.
  //         -
  bool AVIN(const AttributeMap &Attrs, unsigned MBBNum, unsigned Elt) const {
    auto Predecessors = blockPredecessors(MBBNum);
    if (Predecessors.begin() == Predecessors.end())
      return false;
    return all_of(Predecessors, [&](const MachineBasicBlock *P) {
      return Attrs[blockNumber(P->getNumber())].AVOUT.test(Elt);
    });
  }

  /// Determine uses based on ShrinkWrapInfo.
  // FIXME: ShrinkWrap2: Remove. Call SWI directly.
  void determineUses();
  /// Remove uses and fill NoReturnBlocks with the blocks that we know are not
  /// going to return from the function.
  /// FIXME: ShrinkWrap2: Is this the correct place to compute this?
  void removeUsesOnNoReturnPaths();
  void dumpUses() const;
  /// Mark all the basic blocks / SCCs around a loop (pred, succ) as used,
  /// if there is an usage of a CSR inside a loop. We want to avoid any save /
  /// restore operations inside a loop.
  void markUsesOutsideLoops();

  /// Compute the attributes for one element.
  // FIXME: ShrinkWrap2: Don't do this per element.
  void computeAttributes(
      unsigned Elt, AttributeMap &Attrs,
      ReversePostOrderTraversal<const MachineFunction *> &RPOT) const;
  /// Save the results for this particular element.
  // FIXME: ShrinkWrap2: Don't do this per element.
  void gatherAttributesResults(unsigned Elt, AttributeMap &Attrs);
  /// Check for critical edges and mark new blocks as needed.
  // FIXME: ShrinkWrap2: Don't do this per element.
  bool hasCriticalEdges(unsigned Elt, AttributeMap &Attrs);
  /// Dump the contents of the attributes.
  // FIXME: ShrinkWrap2: Don't do this per element.
  void dumpAttributes(unsigned Elt, const AttributeMap &Attrs) const;

  /// * Verify if the results are better than obvious results, like:
  ///   * CSR used in a single MBB: only one save and one restore.
  /// * Remove empty entries from the Saves / Restores maps.
  void postProcessResults();
  /// Compute the shrink-wrapping cost, which is based on block frequency.
  unsigned computeShrinkWrappingCost(MachineBlockFrequencyInfo *MBFI) const;
  /// Compute the same cost, in entry / return blocks, which is based on block
  /// frequency.
  unsigned computeDefaultCost(MachineBlockFrequencyInfo *MBFI) const;
  /// Verify save / restore points by walking the CFG.
  /// This asserts if anything went wrong.
  void verifySavesRestores() const;

  unsigned numberOfUselessSaves() const;

  /// Dump the final shrink-wrapping results.
  void dumpResults() const;

public:
  /// Run the shrink-wrapper on the function. If there are no uses, there will
  /// be no saves / restores.
  /// By default, run the shrink-wrapper with the target's CSRShrinkWrapInfo.
  ShrinkWrapper(const MachineFunction &MF);
  /// Run the shrink-wrapper with a custom ShrinkWrapInfo.
  ShrinkWrapper(const MachineFunction &MF, std::unique_ptr<ShrinkWrapInfo> SWI);

  /// Check if the function has any uses that can be shrink-wrapped.
  bool hasUses() const { return !Uses.empty(); }

  /// Get the target's shrink-wrap info.
  ShrinkWrapInfo &getSWI() { return *SWI; };
  const ShrinkWrapInfo &getSWI() const { return *SWI; };

  /// Get the final results.
  const SparseBBResultSetMap &getSaves() { return Saves; }
  const SparseBBResultSetMap &getRestores() { return Restores; }

  /// Emit optimization remarks for the whole function.
  void emitRemarks(MachineOptimizationRemarkEmitter *ORE,
                   MachineBlockFrequencyInfo *MBFI) const;

  /// Check that the final results are better than the default behaviour.
  bool areResultsInteresting(MachineBlockFrequencyInfo *MBFI) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SHRINKWRAP_H
