//===- DominatorShrinkWrapper.cpp - Shrink Wrapping Utility -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Implementation of the DominatorShrinkWrap(per / Info).
//===----------------------------------------------------------------------===//

#include "DominatorShrinkWrapper.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

STATISTIC(NumCandidates, "Number of shrink-wrapping candidates");
STATISTIC(NumCandidatesDropped,
          "Number of shrink-wrapping candidates dropped because of frequency");

/// \brief Helper function to find the immediate (post) dominator.
template <typename ListOfBBs, typename DominanceAnalysis>
static MachineBasicBlock *FindIDom(MachineBasicBlock &Block, ListOfBBs BBs,
                                   DominanceAnalysis &Dom) {
  MachineBasicBlock *IDom = &Block;
  for (MachineBasicBlock *BB : BBs) {
    IDom = Dom.findNearestCommonDominator(IDom, BB);
    if (!IDom)
      break;
  }
  if (IDom == &Block)
    return nullptr;
  return IDom;
}

void DominatorShrinkWrapper::updateSaveRestorePoints(MachineBasicBlock &MBB,
                                                     unsigned Elt) {

  MachineBasicBlock *&Save = Saves[Elt];
  MachineBasicBlock *&Restore = Restores[Elt];

  // Get rid of the easy cases first.
  if (!Save)
    Save = &MBB;
  else
    Save = MDT.findNearestCommonDominator(Save, &MBB);

  if (!Save) {
    DEBUG(dbgs() << "Found a block that is not reachable from Entry\n");
    return;
  }

  if (!Restore)
    Restore = &MBB;
  else if (MPDT.getNode(&MBB)) // If the block is not in the post dom tree, it
                               // means the block never returns. If that's the
                               // case, we don't want to call
                               // `findNearestCommonDominator`, which will
                               // return `Restore`.
    Restore = MPDT.findNearestCommonDominator(Restore, &MBB);
  else
    stopTrackingElt(Elt); // Abort, we can't find a restore point in this case.

  if (!Restore) {
    DEBUG(dbgs() << "Restore point needs to be spanned on several blocks\n");
    return;
  }

  // Make sure Save and Restore are suitable for shrink-wrapping:
  // 1. all path from Save needs to lead to Restore before exiting.
  // 2. all path to Restore needs to go through Save from Entry.
  // We achieve that by making sure that:
  // A. Save dominates Restore.
  // B. Restore post-dominates Save.
  // C. Save and Restore are in the same loop.
  bool SaveDominatesRestore = false;
  bool RestorePostDominatesSave = false;
  while (Save && Restore &&
         (!(SaveDominatesRestore = MDT.dominates(Save, Restore)) ||
          !(RestorePostDominatesSave = MPDT.dominates(Restore, Save)) ||
          // Post-dominance is not enough in loops to ensure that all uses/defs
          // are after the prologue and before the epilogue at runtime.
          // E.g.,
          // while(1) {
          //  Save
          //  Restore
          //   if (...)
          //     break;
          //  use/def CSRs
          // }
          // All the uses/defs of CSRs are dominated by Save and post-dominated
          // by Restore. However, the CSRs uses are still reachable after
          // Restore and before Save are executed.
          //
          // For now, just push the restore/save points outside of loops.
          // FIXME: Refine the criteria to still find interesting cases
          // for loops.
          MLI.getLoopFor(Save) || MLI.getLoopFor(Restore))) {
    // Fix (A).
    if (!SaveDominatesRestore) {
      Save = MDT.findNearestCommonDominator(Save, Restore);
      continue;
    }
    // Fix (B).
    if (!RestorePostDominatesSave)
      Restore = MPDT.findNearestCommonDominator(Restore, Save);

    // Fix (C).
    if (Save && Restore && (MLI.getLoopFor(Save) || MLI.getLoopFor(Restore))) {
      if (MLI.getLoopDepth(Save) > MLI.getLoopDepth(Restore)) {
        // Push Save outside of this loop if immediate dominator is
        // different from save block. If immediate dominator is not
        // different, bail out.
        Save = FindIDom<>(*Save, Save->predecessors(), MDT);
        if (!Save)
          break;
      } else {
        // If the loop does not exit, there is no point in looking
        // for a post-dominator outside the loop.
        SmallVector<MachineBasicBlock *, 4> ExitBlocks;
        MLI.getLoopFor(Restore)->getExitingBlocks(ExitBlocks);
        // Push Restore outside of this loop.
        // Look for the immediate post-dominator of the loop exits.
        MachineBasicBlock *IPdom = Restore;
        for (MachineBasicBlock *LoopExitBB : ExitBlocks) {
          IPdom = FindIDom<>(*IPdom, LoopExitBB->successors(), MPDT);
          if (!IPdom)
            break;
        }
        // If the immediate post-dominator is not in a less nested loop,
        // then we are stuck in a program with an infinite loop.
        // In that case, we will not find a safe point, hence, bail out.
        if (IPdom && MLI.getLoopDepth(IPdom) < MLI.getLoopDepth(Restore))
          Restore = IPdom;
        else {
          stopTrackingElt(Elt);
          break;
        }
      }
    }
  }
}

DominatorShrinkWrapper::DominatorShrinkWrapper(MachineFunction &MF,
                                               const ShrinkWrapInfo &SWI,
                                               MachineDominatorTree &MDT,
                                               MachinePostDominatorTree &MPDT,
                                               MachineBlockFrequencyInfo &MBFI,
                                               MachineLoopInfo &MLI)
    : ShrinkWrapper(), MDT(MDT), MPDT(MPDT), MLI(MLI), Saves(SWI.getNumElts()),
      Restores(SWI.getNumElts()), Tracking(SWI.getAllUsedElts()),
      Entry(MF.front()) {
  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&*MF.begin());
  if (containsIrreducibleCFG<MachineBasicBlock *>(RPOT, MLI)) {
    // If MF is irreducible, a block may be in a loop without
    // MachineLoopInfo reporting it. I.e., we may use the
    // post-dominance property in loops, which lead to incorrect
    // results. Moreover, we may miss that the prologue and
    // epilogue are not in the same loop, leading to unbalanced
    // construction/deconstruction of the stack frame.
    DEBUG(dbgs() << "Irreducible CFGs are not supported yet\n");
    stopTrackingAllElts();
    return;
  }

  for (MachineBasicBlock &MBB : MF) {
    if (!hasElementToTrack())
      return;
    DEBUG(dbgs() << "Look into: " << printMBBReference(MBB) << '\n');
    if (MBB.isEHPad()) {
      // Push the prologue and epilogue outside of
      // the region that may throw by making sure
      // that all the landing pads are at least at the
      // boundary of the save and restore points.
      // The problem with exceptions is that the throw
      // is not properly modeled and in particular, a
      // basic block can jump out from the middle.
      DEBUG(dbgs() << printMBBReference(MBB) << " is an EHPad.");
      for (unsigned Elt : SWI.getAllUsedElts().set_bits()) {
        updateSaveRestorePoints(MBB, Elt);
        if (!arePointsInteresting(Elt)) {
          DEBUG(dbgs() << "EHPad prevents shrink-wrapping\n");
          stopTrackingElt(Elt);
          continue;
        }
      }
      continue;
    }

    if (const BitVector *Uses = SWI.getUses(MBB.getNumber())) {
      for (unsigned Elt : Uses->set_bits()) {
        if (!stillTrackingElt(Elt))
          continue;
        // FIXME: Add custom printing through SWI.
        DEBUG(dbgs() << "For elt " << Elt << ":\n");
        // Save (resp. restore) point must dominate (resp. post dominate)
        // MI. Look for the proper basic block for those.
        updateSaveRestorePoints(MBB, Elt);
        // If we are at a point where we cannot improve the placement of
        // save/restore instructions, just give up.
        if (!arePointsInteresting(Elt)) {
          DEBUG(dbgs() << "No Shrink wrap candidate found\n");
          stopTrackingElt(Elt);
          continue;
        }
      }
    }
  }

  for (unsigned Elt : Tracking.set_bits()) {
    MachineBasicBlock *&Save = Saves[Elt];
    MachineBasicBlock *&Restore = Restores[Elt];

    DEBUG(dbgs() << "Post-process elt " << Elt << ":\n");
    if (!arePointsInteresting(Elt)) {
      // If the points are not interesting at this point, then they must be
      // null because it means we did not encounter any frame/CSR related
      // code. Otherwise, we would have returned from the previous loop.
      assert(!Save && !Restore && "We miss a shrink-wrap opportunity?!");
      DEBUG(dbgs() << "Nothing to shrink-wrap\n");
      continue;
    }

    uint64_t EntryFreq = MBFI.getEntryFreq();
    DEBUG(dbgs() << "\n ** Results **\nFrequency of the Entry: " << EntryFreq
                 << '\n');

    const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
    do {
      DEBUG(dbgs() << "Shrink wrap candidates (#, Name, Freq):\nSave: "
                   << Save->getNumber() << ' ' << Save->getName() << ' '
                   << MBFI.getBlockFreq(Save).getFrequency() << "\nRestore: "
                   << Restore->getNumber() << ' ' << Restore->getName() << ' '
                   << MBFI.getBlockFreq(Restore).getFrequency() << '\n');

      bool IsSaveCheap, TargetCanUseSaveAsPrologue = false;
      if (((IsSaveCheap =
                EntryFreq >= MBFI.getBlockFreq(Save).getFrequency()) &&
           EntryFreq >= MBFI.getBlockFreq(Restore).getFrequency()) &&
          ((TargetCanUseSaveAsPrologue = TFI->canUseAsPrologue(*Save)) &&
           TFI->canUseAsEpilogue(*Restore)))
        break;
      DEBUG(
          dbgs() << "New points are too expensive or invalid for the target\n");
      MachineBasicBlock *NewBB;
      if (!IsSaveCheap || !TargetCanUseSaveAsPrologue) {
        Save = FindIDom<>(*Save, Save->predecessors(), MDT);
        if (!Save)
          break;
        NewBB = Save;
      } else {
        // Restore is expensive.
        Restore = FindIDom<>(*Restore, Restore->successors(), MPDT);
        if (!Restore)
          break;
        NewBB = Restore;
      }
      updateSaveRestorePoints(*NewBB, Elt);
    } while (Save && Restore);

    if (!arePointsInteresting(Elt)) {
      ++NumCandidatesDropped;
      stopTrackingElt(Elt);
      continue;
    }

    DEBUG(dbgs() << "Final shrink wrap candidates:\nSave: " << Save->getNumber()
                 << ' ' << Save->getName() << "\nRestore: "
                 << Restore->getNumber() << ' ' << Restore->getName() << '\n');

    /// "return" the blocks where we need to save / restore this elt.
    /// In this case, only one save and one restore point is returned.
    BitVector &SaveResult = SavesResult[Save->getNumber()];
    if (SaveResult.empty())
      SaveResult.resize(SWI.getNumElts());
    SaveResult.set(Elt);

    BitVector &RestoreResult = RestoresResult[Restore->getNumber()];
    if (RestoreResult.empty())
      RestoreResult.resize(SWI.getNumElts());
    RestoreResult.set(Elt);

    ++NumCandidates;
  }
}
