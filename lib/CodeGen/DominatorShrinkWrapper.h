//===- DominatorShrinkWrapper.h - Dominator-based shrink-wrapping *- C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass looks for safe point where the prologue and epilogue can be
// inserted.
// The safe point for the prologue (resp. epilogue) is called Save
// (resp. Restore).
// A point is safe for prologue (resp. epilogue) if and only if
// it 1) dominates (resp. post-dominates) all the frame related operations and
// between 2) two executions of the Save (resp. Restore) point there is an
// execution of the Restore (resp. Save) point.
//
// For instance, the following points are safe:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Restore -> Save -> Restore ...
// And the following points are not:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
// }
// for (int i = 0; i < 10; ++i) {
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Save -> ... -> Restore -> Restore.
//
// This pass also ensures that the safe points are 3) cheaper than the regular
// entry and exits blocks.
//
// Property #1 is ensured via the use of MachineDominatorTree and
// MachinePostDominatorTree.
// Property #2 is ensured via property #1 and MachineLoopInfo, i.e., both
// points must be in the same loop.
// Property #3 is ensured via the MachineBlockFrequencyInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DOMINATOR_SHRINKWRAPPER_H
#define LLVM_CODEGEN_DOMINATOR_SHRINKWRAPPER_H

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ShrinkWrapper.h"
#include "llvm/Support/Debug.h"

namespace llvm {

class MachineDominatorTree;
class MachineLoopInfo;
struct MachinePostDominatorTree;

/// \brief Class to determine where the safe point to insert the
/// prologue and epilogue are.
/// Unlike the paper from Fred C. Chow, PLDI'88, that introduces the
/// shrink-wrapping term for prologue/epilogue placement, this pass
/// does not rely on expensive data-flow analysis. Instead we use the
/// dominance properties and loop information to decide which point
/// are safe for such insertion.
class DominatorShrinkWrapper : public ShrinkWrapper {
  /// This algorithm is based on common dominators.
  MachineDominatorTree &MDT;
  MachinePostDominatorTree &MPDT;

  /// Hold the loop information. Used to determine if Save and Restore
  /// are in the same loop.
  MachineLoopInfo &MLI;

  /// Current safe point found for saving each element.
  /// Example:
  /// The prologue will be inserted before the first instruction
  /// in this basic block.
  SmallVector<MachineBasicBlock *, 1> Saves;

  /// Current safe point found for restoring each element.
  /// Example:
  /// The epilogue will be inserted before the first terminator instruction
  /// in this basic block.
  SmallVector<MachineBasicBlock *, 1> Restores;

  // Still tracking.
  BitVector Tracking;

  /// Entry block.
  const MachineBasicBlock &Entry;

  /// Check whether or not Save and Restore points are still interesting for
  /// shrink-wrapping.
  bool arePointsInteresting(unsigned Elt) const {
    return Saves[Elt] != &Entry && Saves[Elt] && Restores[Elt];
  }

  /// Check if we're still tracking \p Elt or we already gave up on it.
  bool stillTrackingElt(unsigned Elt) const { return Tracking.test(Elt); }
  /// Check if we still have elements to track.
  bool hasElementToTrack() const { return Tracking.any(); }

  /// Mark this element as untracked.
  void stopTrackingElt(unsigned Elt) {
    Tracking.reset(Elt);
    Saves[Elt] = nullptr;
    Restores[Elt] = nullptr;
  }
  /// Mark all elements as untracked.
  void stopTrackingAllElts() {
    Tracking.reset();
  }

  /// \brief Update the Save and Restore points such that \p MBB is in
  /// the region that is dominated by Save and post-dominated by Restore
  /// and Save and Restore still match the safe point definition.
  /// Such point may not exist and Save and/or Restore may be null after
  /// this call.
  void updateSaveRestorePoints(MachineBasicBlock &MBB, unsigned Elt);

public:
  DominatorShrinkWrapper(MachineFunction &MF, const ShrinkWrapInfo &SWI,
                         MachineDominatorTree &MDT,
                         MachinePostDominatorTree &MPDT,
                         MachineBlockFrequencyInfo &MBFI, MachineLoopInfo &MLI);

  /// Return the elements that are tracked in the final result.
  /// Untracked elements won't show up in the maps returned by `getSaves()` and
  /// `getRestores()`. The usual behavior is to add them to the entry and
  /// return blocks, but that decision is left to the caller.
  const BitVector &getTrackedElts() { return Tracking; }
};

} // end namespace llvm
#endif // LLVM_CODEGEN_DOMINATOR_SHRINKWRAPPER_H
