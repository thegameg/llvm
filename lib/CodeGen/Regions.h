// Beginning >>>>>>>>>>>>>>>>>>>>>>>
#define UseRegions cl::BOU_FALSE
#if 0
static cl::opt<cl::boolOrDefault>
    UseRegions("shrink-wrap-use-regions", cl::Hidden,
               cl::desc("push saves / restores at region boundaries"));
#endif

// Includes >>>>>>>>>>>>>>>>>>>>

// Determine maximal SESE regions for save / restore blocks placement.
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegionInfo.h"


// In class attr >>>>>>>>>>>>>>>>>>>>>>>>>
// REGIONS ===================================================================

  /// (Post)Dominator trees used for getting maximal SESE regions.
  MachineDominatorTree *MDT;
  MachinePostDominatorTree *MPDT;

  /// SESE region information used by Christopher Lupo and Kent D. Wilken's
  /// paper.
  MachineRegionInfo *RegionInfo;
  /// Hold all the maximal regions.
  // FIXME: ShrinkWrap2: Use something better than std::set?
  std::set<MachineRegion *> MaximalRegions;
// In class func >>>>>>>>>>>>>>>>>>>>>>>>>
/// Populate MaximalRegions with all the regions in the function.
  // FIXME: ShrinkWrap2: iterate on depth_first(Top) should work?
  // FIXME: ShrinkWrap2: use a lambda inside computeMaximalRegions?
  void gatherAllRegions(MachineRegion *Top) {
    MaximalRegions.insert(Top);
    for (const std::unique_ptr<MachineRegion> &MR : *Top)
      gatherAllRegions(MR.get());
  }

  /// The SESE region information used by Christopher Lupo and Kent D. Wilken's
  /// paper is based on _maximal_ SESE regions which are described like this:
  /// A SESE region (a, b) is _maximal_ provided:
  /// * b post-dominates b' for any SESE region (a, b')
  /// * a dominates a' for any SESE region (a', b)
  // FIXME: ShrinkWrap2: Somehow, we have RegionInfoBase::getMaxRegionExit that
  // returns the exit of the maximal refined region starting at a basic block.
  // Using this doesn't return the blocks expected by the paper's definition of
  // _maximal_ regions.
  // FIXME: ShrinkWrap2: Same here, he have RegionBase::getExpandedRegion that
  // returns a bigger region starting at the same entry point.
  // FIXME: ShrinkWrap2: Merge with gatherAllRegions?
  void computeMaximalRegions(MachineRegion *Top);
  void removeMaximalRegion(MachineRegion *Parent, MachineRegion *Child,
                           MachineRegion *TransferTo);
  void mergeMaximalRegions(MachineRegion *Entry, MachineRegion *Exit);

  /// Move all the save / restore points at the boundaries of a maximal region.
  /// This is where [2] is an improvement of [1].
  // FIXME: ShrinkWrap2: Don't ALWAYS move. Use cost model.
  void moveAtRegionBoundaries(MachineFunction &MF);

  // In class init() <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  if (UseRegions != cl::BOU_FALSE) {
      MDT = &getAnalysis<MachineDominatorTree>();
      MPDT = &getAnalysis<MachinePostDominatorTree>();
      RegionInfo = &getAnalysis<MachineRegionInfoPass>().getRegionInfo();
    }
  // in class getAnalysisUsage <<<<<<<<<<<<<<<
  if (UseRegions != cl::BOU_FALSE) {
      AU.addRequired<MachineDominatorTree>();
      AU.addRequired<MachinePostDominatorTree>();
      AU.addRequired<MachineRegionInfoPass>();
    }

  // in class clear() <<<<<<<<<<<<<<<
    if (UseRegions != cl::BOU_FALSE)
      MaximalRegions.clear();

  // INITIALIZE_PASS_DEPEN <<<<<<<<<<<<<<<
  INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
  INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
  INITIALIZE_PASS_DEPENDENCY(MachineRegionInfoPass)

  // runOnMachineFunction before shrink-wrapping results <<<<<<<<<<<<<<<
  if (UseRegions != cl::BOU_FALSE) {
      DEBUG(dbgs() << "******** Optimize using regions\n";);
      gatherAllRegions(RegionInfo->getTopLevelRegion());
      DEBUG(RegionInfo->dump());
      computeMaximalRegions(RegionInfo->getTopLevelRegion());
      DEBUG(RegionInfo->dump());
      moveAtRegionBoundaries(MF);
      DEBUG(dbgs() << "******** After regions\n";);
      DEBUG(dumpResults(MF));
    }


void ShrinkWrap2::computeMaximalRegions(MachineRegion *Top) {
  using RegionPair = std::pair<MachineRegion *, MachineRegion *>;
  bool Changed = true;

  while (Changed) {
    Changed = false;

    // Merge consecutive regions. If Exit(A) == Entry(B), merge A and B.
    bool MergeChanged = true;
    while (MergeChanged) {
      MergeChanged = false;

      // FIXME: ShrinkWrap2: Don't search all of them.
      for (MachineRegion *MR : MaximalRegions) {
        auto Found = std::find_if(
            MaximalRegions.begin(), MaximalRegions.end(),
            [MR](MachineRegion *M) { return MR->getExit() == M->getEntry(); });
        if (Found != MaximalRegions.end()) {
          MergeChanged = true;
          Changed = true;
          mergeMaximalRegions(MR, *Found);
          break;
        }
      }
    }

    SmallVector<RegionPair, 4> ToRemove;
    auto FlushRemoveQueue = [&] {
      // We need to remove this after the loop so that we don't invalidate
      // iterators.
      Changed |= !ToRemove.empty();
      // Sort by depth. Start with the deepest regions, to preserve the outer
      // regions.
      std::sort(ToRemove.begin(), ToRemove.end(),
                [](RegionPair A, RegionPair B) {
                  return A.second->getDepth() > B.second->getDepth();
                });

      // Remove the regions and transfer all children.
      for (auto &KV : ToRemove) {
        MachineRegion *Parent = KV.first;
        MachineRegion *Child = KV.second;
        removeMaximalRegion(Parent, Child, Parent);
      }

      ToRemove.clear();
    };

    // Look for sub-regions that are post dominated by the exit and dominated by
    // the entry of a maximal region, and remove them in order to have only
    // maximal regions.
    // FIXME: ShrinkWrap2: Avoid double pass. DF?
    DEBUG(RegionInfo->dump());
    for (MachineRegion *Parent : MaximalRegions) {
      for (const std::unique_ptr<MachineRegion> &Child : *Parent) {
        if (Child->getEntry() == Parent->getEntry() &&
            (MPDT->dominates(Parent->getExit(), Child->getExit()) ||
             Parent->isTopLevelRegion())) {
          DEBUG(dbgs() << "(MPDT) Schedule for erasing : "
                       << Child->getNameStr() << '\n');
          ToRemove.emplace_back(Parent, Child.get());
        }
        if (Child->getExit() == Parent->getExit() &&
            MDT->dominates(Parent->getEntry(), Child->getEntry())) {
          DEBUG(dbgs() << "(MDT) Schedule for erasing : " << Child->getNameStr()
                       << '\n');
          ToRemove.emplace_back(Parent, Child.get());
        }
      }
    }

    FlushRemoveQueue();

    // Here, we try to extract regions from loops.
    // If a region is inside a loop, first, we try to make it start at the
    // pre-header of the loop. If that's impossible, we just remove the region.
    // FIXME: ShrinkWrap2: This feels like a hack, I guess the cost model might
    // help here?.
    for (MachineRegion *Region : MaximalRegions) {
      if (MachineLoop *Loop = MLI->getLoopFor(Region->getEntry())) {
        // FIXME: ShrinkWrap2: Really ugly.
        if (MachineBasicBlock *Preheader = [&] {
              if (MachineBasicBlock *Preheader = MLI->findLoopPreheader(
                      Loop, /* SpeculativePreheader */ true)) {
                if (Preheader != Region->getEntry())
                  return Preheader;
                if (MachineLoop *ParentLoop = Loop->getParentLoop())
                  if (MachineBasicBlock *ParentPreheader =
                          MLI->findLoopPreheader(
                              ParentLoop, /* SpeculativePreheader */ true))
                    if (ParentPreheader != Region->getEntry())
                      return ParentPreheader;
              }
              return static_cast<MachineBasicBlock *>(nullptr);
            }()) {
          // If we found a preheader, we can move the entry here.
          DEBUG(dbgs() << "Replacing entry for: " << Region->getNameStr()
                       << " with " << Preheader->getName() << '\n');
          Region->replaceEntry(Preheader);
          Changed = true;
        } else {
          // If no preheader is found, remove the region, to be safe.
          ToRemove.emplace_back(Region, Region->getParent());
        }
      }
    }

    FlushRemoveQueue();
  }

  // Reconstruct the MRegion -> MBB association inside the RegionInfo.
  // FIXME: ShrinkWrap2: Quick and dirty.
  for (MachineBasicBlock &MBB : *Top->getEntry()->getParent()) {
    int Depth = std::numeric_limits<int>::min();
    MachineRegion *Final = nullptr;
    for (MachineRegion *MR : MaximalRegions) {
      if (MR->contains(&MBB) || MR->getExit() == &MBB) {
        if (static_cast<int>(MR->getDepth()) > Depth) {
          Depth = MR->getDepth();
          Final = MR;
        }
      }
    }
    assert(Depth >= 0 && "Minimal depth is the top level region, 0.");
    RegionInfo->setRegionFor(&MBB, Final);
  }

  DEBUG(for (MachineRegion *R : MaximalRegions) R->print(dbgs(), false););
}
void ShrinkWrap2::removeMaximalRegion(MachineRegion *Parent,
                                      MachineRegion *Child,
                                      MachineRegion *TransferTo) {
  DEBUG(dbgs() << "Erasing region: " << Child->getNameStr()
               << " from: " << Parent->getNameStr()
               << " and transfer children to: " << TransferTo->getNameStr()
               << '\n');
  MaximalRegions.erase(Child);
  Child->transferChildrenTo(TransferTo);
  Parent->removeSubRegion(Child);
}
void ShrinkWrap2::mergeMaximalRegions(MachineRegion *Entry,
                                      MachineRegion *Exit) {
  assert(Entry->getExit() == Exit->getEntry() &&
         "Only merging of regions that share entry(Exit) & exit(Entry) block!");

  DEBUG(dbgs() << "Merging: " << Entry->getNameStr() << " with "
               << Exit->getNameStr() << '\n');

  // Create a region starting at the entry of the first block, ending at the
  // exit of the last block.
  // FIXME: ShrinkWrap2: Leak.
  // FIXME: ShrinkWrap2: Use MachineRegionInfo->create?
  MachineRegion *NewRegion =
      new MachineRegion(Entry->getEntry(), Exit->getExit(), RegionInfo, MDT);
  MaximalRegions.insert(NewRegion);
  // Attach this region to the common parent of both Entry and Exit
  // blocks.
  MachineRegion *NewParent = RegionInfo->getCommonRegion(Entry, Exit);
  NewParent->addSubRegion(NewRegion);

  // Remove the subregions that were merged.
  removeMaximalRegion(Entry->getParent(), Entry, NewRegion);
  removeMaximalRegion(Exit->getParent(), Exit, NewRegion);
}
void ShrinkWrap2::moveAtRegionBoundaries(MachineFunction &MF) {
  BBSet ToRemove{MF.getNumBlockIDs()};
  SmallVector<std::pair<unsigned, const RegSet *>, 4> ToInsert;

  auto FlushInsertRemoveQueue = [&](SparseBBRegSetMap &Map) {
    // Move registers to the new basic block.
    for (auto &KV : ToInsert) {
      unsigned MBBNum = KV.first;
      const RegSet &Regs = *KV.second;
      Map[MBBNum] |= Regs;
    }
    ToInsert.clear();

    // Remove the basic blocks.
    FOREACH_BIT(MBBNum, ToRemove) { Map.erase(MBBNum); }
    ToRemove.reset();
  };

  // For each saving block, find the smallest region that contains it, and move
  // the saves at the entry of the region.
  // FIXME: ShrinkWrap2: Cost model.
  for (auto &KV : Saves) {
    MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
    const RegSet &Regs = KV.second;

    MachineRegion *MR = RegionInfo->getRegionFor(MBB);
    assert(MR);
    if (MBB == MR->getEntry())
      continue;
    DEBUG(dbgs() << "Moving saves from " << MBB->getNumber() << " to "
                 << MR->getEntry()->getNumber() << '\n');
    ToInsert.emplace_back(MR->getEntry()->getNumber(), &Regs);
    ToRemove.set(MBB->getNumber());
    // If we're moving to top level, and there is no restore related to this
    // save, add a restore at return blocks. This can happen in the following
    // case:
    //      0 ---> 2
    //      |
    //      |
    //      v ->
    //      1    3 // infinite loop
    //        <-
    // if there is an use of a CSR for the basic block 1.
    if (MR->isTopLevelRegion()) {
      FOREACH_BIT(Reg, Regs) {
        unsigned NumSaves =
            std::count_if(Saves.begin(), Saves.end(),
                          [&](const std::pair<unsigned, RegSet> &KV) {
                            return KV.second.test(Reg);
                          });
        bool HasRestores =
            std::find_if(Restores.begin(), Restores.end(),
                         [&](const std::pair<unsigned, RegSet> &KV) {
                           return KV.second.test(Reg);
                         }) != Restores.end();
        if (NumSaves == 1 && !HasRestores) {
          // Restore in all the return blocks.
          for (MachineBasicBlock &MBB : MF) {
            if (MBB.isReturnBlock()) {
              RegSet &RestoresMBB = Restores[MBB.getNumber()];
              if (RestoresMBB.empty()) {
                const TargetRegisterInfo &TRI =
                    *MF.getSubtarget().getRegisterInfo();
                RestoresMBB.resize(TRI.getNumRegs());
              }
              RestoresMBB.set(Reg);
            }
          }
        }
      }
    }
  }

  // Actually apply the insert / removes to the saves.
  FlushInsertRemoveQueue(Saves);

  // For each restoring block, find the smallest region that contains it and
  // move the saves at the entry of the region.
  // FIXME: ShrinkWrap2: Cost model.
  for (auto &KV : Restores) {
    MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
    RegSet &Regs = KV.second;

    MachineRegion *MR = RegionInfo->getRegionFor(MBB);
    assert(MR);
    // Corner case of the top level region. The restores have to be added on all
    // the terminator blocks.
    if (MR->isTopLevelRegion()) {
      // FIXME: ShrinkWrap2: Better way to do this?
      for (MachineBasicBlock &Block : MF) {
        if (Block.isReturnBlock()) {
          if (MBB == &Block)
            continue;
          DEBUG(dbgs() << "Moving restores from " << MBB->getNumber() << " to "
                       << Block.getNumber() << '\n');
          ToInsert.emplace_back(Block.getNumber(), &Regs);
          if (!MBB->isReturnBlock())
            ToRemove.set(MBB->getNumber());
        }
      }
    } else {
      if (MBB == MR->getExit())
        continue;
      DEBUG(dbgs() << "Moving restores from " << MBB->getNumber() << " to "
                   << MR->getExit()->getNumber() << '\n');
      ToInsert.emplace_back(MR->getExit()->getNumber(), &Regs);
      ToRemove.set(MBB->getNumber());
    }
  }

  // Actually apply the insert / removes to the restores.
  FlushInsertRemoveQueue(Restores);

  // Eliminate nested saves / restores.
  // FIXME: ShrinkWrap2: Is this really needed? How do we end up here?
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  auto EliminateNested = [&](SparseBBRegSetMap &Map,
                             MachineBasicBlock *(MachineRegion::*Member)()
                                 const) {
    for (auto &KV : Map) {
      MachineBasicBlock *MBB = MF.getBlockNumbered(KV.first);
      RegSet &Regs = KV.second;

      const MachineRegion *Parent = RegionInfo->getRegionFor(MBB);
      DEBUG(dbgs() << "Region for BB#" << MBB->getNumber() << " : "
                   << Parent->getNameStr() << '\n');
      for (const std::unique_ptr<MachineRegion> &Child : *Parent) {
        auto ChildToo = Map.find((Child.get()->*Member)()->getNumber());
        if (ChildToo != Map.end()) {
          RegSet &ChildRegs = ChildToo->second;
          if (&Regs == &ChildRegs)
            continue;
          // For each saved register in the parent, remove them from the
          // children.

          FOREACH_BIT(Reg, Regs) {
            if (ChildRegs[Reg]) {
              DEBUG(dbgs() << "Both " << (Child.get()->*Member)()->getNumber()
                           << " and " << MBB->getNumber() << " save / restore "
                           << PrintReg(Reg, &TRI) << '\n');
              ChildRegs[Reg] = false;
              if (ChildRegs.empty())
                Map.erase(ChildToo);
            }
          }
        }
      }
    }
  };

  EliminateNested(Saves, &MachineRegion::getEntry);
  EliminateNested(Restores, &MachineRegion::getExit);
}
