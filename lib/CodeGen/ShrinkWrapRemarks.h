// FIXME: ShrinkWrap2: DUPLICATE OF SHRINKWRAP2!!! MEGAULTRAGIGAHACK!!!
using RegSet = BitVector;
using SparseBBRegSetMap = DenseMap<unsigned, RegSet>;
using BBRegSetMap = SmallVector<RegSet, 8>;
/// The dataflow attributes needed to compute shrink-wrapping locations.
struct DataflowAttributes {
  /// The MachineFunction we're analysing.
  MachineFunction &MF;

  // FIXME: ShrinkWrap2: Explain anticipated / available and how the
  // properties are used.

  const SparseBBRegSetMap &APP;
  /// Is the register anticipated at the end of this basic block?
  BBRegSetMap ANTOUT;
  /// Is the register anticipated at the beginning of this basic block?
  BBRegSetMap ANTIN;
  /// Is the register available at the beginning of this basic block?
  BBRegSetMap AVIN;
  /// Is the register available at the end of this basic block?
  BBRegSetMap AVOUT;

  DataflowAttributes(MachineFunction &TheFunction,
                     const SparseBBRegSetMap &Used)
      : MF{TheFunction}, APP{Used} {
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    for (BBRegSetMap *Map : {&ANTOUT, &ANTIN, &AVIN, &AVOUT}) {
      Map->resize(MF.getNumBlockIDs());
      for (RegSet &Regs : *Map)
        Regs.resize(TRI.getNumRegs());
    }
  }

  /// Populate the attribute maps with trivial properties from the used
  /// registers.
  void populate() {
    for (auto &KV : APP) {
      unsigned MBBNum = KV.first;
      const RegSet &Regs = KV.second;
      // Setting APP also affects ANTIN and AVOUT.
      // ANTIN = APP || ANTOUT
      ANTIN[MBBNum] |= Regs;
      // AVOUT = APP || AVIN
      AVOUT[MBBNum] |= Regs;
    }
  }
  /// Compute the attributes for one register.
  // FIXME: ShrinkWrap2: Don't do this per register.
  void compute(unsigned Reg) {
    // FIXME: ShrinkWrap2: Reverse DF for ANT? DF for AV?
    // FIXME: ShrinkWrap2: Use a work list, don't compute attributes that
    // we're
    // sure will never change.
    bool Changed = true;
    auto AssignIfDifferent = [&](RegSet &Regs, bool New) {
      if (Regs.test(Reg) != New) {
        Regs.flip(Reg);
        Changed = true;
      }
    };
    auto UsesReg = [&](unsigned MBBNum) {
      auto Found = APP.find(MBBNum);
      if (Found == APP.end())
        return false;
      return Found->second.test(Reg);
    };

    while (Changed) {
      Changed = false;
      for (MachineBasicBlock &MBB : MF) {
        unsigned MBBNum = MBB.getNumber();
        // If there is an use of this register on *all* the paths starting
        // from
        // this basic block, the register is anticipated at the end of this
        // block
        // (propagate the IN attribute of successors to possibly merge saves)
        //           -
        //          | *false*             if no successor.
        // ANTOUT = |
        //          | && ANTIN(succ[i])   otherwise.
        //           -
        RegSet &ANTOUTb = ANTOUT[MBBNum];
        if (MBB.succ_empty())
          AssignIfDifferent(ANTOUTb, false);
        else {
          bool A = std::all_of(MBB.succ_begin(), MBB.succ_end(),
                               [&](MachineBasicBlock *S) {
                                 if (S == &MBB) // Ignore self.
                                   return true;
                                 return ANTIN[S->getNumber()].test(Reg);
                               });
          AssignIfDifferent(ANTOUTb, A);
        }

        // If the register is used in the block, or if it is anticipated in
        // all
        // successors it is also anticipated at the beginning, since we
        // consider
        // entire blocks.
        //          -
        // ANTIN = | APP || ANTOUT
        //          -
        RegSet &ANTINb = ANTIN[MBBNum];
        bool NewANTIN = UsesReg(MBBNum) || ANTOUT[MBBNum].test(Reg);
        AssignIfDifferent(ANTINb, NewANTIN);

        // If there is an use of this register on *all* the paths arriving in
        // this
        // block, then the register is available in this block (propagate the
        // out
        // attribute of predecessors to possibly merge restores).
        //         -
        //        | *false*             if no predecessor.
        // AVIN = |
        //        | && AVOUT(pred[i])   otherwise.
        //         -
        RegSet &AVINb = AVIN[MBBNum];
        if (MBB.pred_empty())
          AssignIfDifferent(AVINb, false);
        else {
          bool A = std::all_of(MBB.pred_begin(), MBB.pred_end(),
                               [&](MachineBasicBlock *P) {
                                 if (P == &MBB) // Ignore self.
                                   return true;
                                 return AVOUT[P->getNumber()].test(Reg);
                               });
          AssignIfDifferent(AVINb, A);
        }

        // If the register is used in the block, or if it is always available
        // in
        // all predecessors , it is also available on exit, since we consider
        // entire blocks.
        //          -
        // AVOUT = | APP || AVIN
        //          -
        RegSet &AVOUTb = AVOUT[MBBNum];
        bool NewAVOUT = UsesReg(MBBNum) || AVIN[MBBNum].test(Reg);
        AssignIfDifferent(AVOUTb, NewAVOUT);
      }
    }
  }
  /// Save the results for this particular register.
  // FIXME: ShrinkWrap2: Don't do this per register.
  void results(unsigned Reg, SparseBBRegSetMap &Saves,
               SparseBBRegSetMap &Restores) {
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

    for (MachineBasicBlock &MBB : MF) {
      unsigned MBBNum = MBB.getNumber();
      // If the register uses are anticipated on *all* the paths leaving this
      // block, and if the register is not available at the entrance of this
      // block
      // (if it is, then it means it has been saved already, but not
      // restored),
      // and if *none* of the predecessors anticipates this register on their
      // output (we want to get the "highest" block), then we can identify a
      // save
      // point for the function.
      //
      // SAVE = ANTIN && !AVIN && !ANTIN(pred[i])
      //
      bool NS = std::none_of(MBB.pred_begin(), MBB.pred_end(),
                             [&](MachineBasicBlock *P) {
                               if (P == &MBB) // Ignore self.
                                 return false;
                               return ANTIN[P->getNumber()].test(Reg);
                             });
      if (ANTIN[MBBNum].test(Reg) && !AVIN[MBBNum].test(Reg) && NS) {
        RegSet &Save = Saves[MBBNum];
        if (Save.empty())
          Save.resize(TRI.getNumRegs());
        Save.set(Reg);
      }

      // If the register uses are available on *all* the paths leading to this
      // block, and if the register is not anticipated at the exit of this
      // block
      // (if it is, then it means it has been restored already), and if *none*
      // of
      // the successors make the register available (we want to cover the
      // deepest
      // use), then we can identify a restrore point for the function.
      //
      // RESTORE = AVOUT && !ANTOUT && !AVOUT(succ[i])
      //
      bool NR = std::none_of(MBB.succ_begin(), MBB.succ_end(),
                             [&](MachineBasicBlock *S) {
                               if (S == &MBB) // Ignore self.
                                 return false;
                               return AVOUT[S->getNumber()].test(Reg);
                             });
      if (AVOUT[MBBNum].test(Reg) && !ANTOUT[MBBNum].test(Reg) && NR) {
        RegSet &Restore = Restores[MBBNum];
        if (Restore.empty())
          Restore.resize(TRI.getNumRegs());
        Restore.set(Reg);
      }
    }
  }
  /// Dump the contents of the attributes.
  // FIXME: ShrinkWrap2: Don't do this per register.
  void dump(unsigned Reg) const {
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    for (MachineBasicBlock &MBB : MF) {
      unsigned MBBNum = MBB.getNumber();
      dbgs() << "BB#" << MBBNum << "<" << PrintReg(Reg, &TRI) << ">"
             << ":\n\tANTOUT : " << ANTOUT[MBBNum].test(Reg) << '\n'
             << "\tANTIN : " << ANTIN[MBBNum].test(Reg) << '\n'
             << "\tAVIN : " << AVIN[MBBNum].test(Reg) << '\n'
             << "\tAVOUT : " << AVOUT[MBBNum].test(Reg) << '\n';
    }
  }
};

// FIXME: ShrinkWrap2: Target hook might add other callee saves.
static SparseBBRegSetMap determineCalleeSaves(const MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SparseBBRegSetMap UsedCSR;

  // Walk all the uses of each callee-saved register, and map them to their
  // basic blocks.
  const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();

  // FIXME: ShrinkWrap2: Naked functions.
  // FIXME: ShrinkWrap2: __builtin_unwind_init.

  // Test all the target's callee saved registers.
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];

    // Check for regmasks.
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isRegMask() && MO.clobbersPhysReg(Reg)) {
            RegSet &Used = UsedCSR[MBB.getNumber()];
            if (Used.empty())
              Used.resize(TRI.getNumRegs());
            Used.set(Reg);
          }
        }
      }
    }

    // If at least one of the aliases is used, mark the original register as
    // used.
    for (MCRegAliasIterator AliasReg(Reg, &TRI, true); AliasReg.isValid();
         ++AliasReg) {
      // Walk all the uses, excepting for debug instructions.
      for (auto MOI = MRI.reg_nodbg_begin(*AliasReg), e = MRI.reg_nodbg_end();
           MOI != e; ++MOI) {
        // Get or create the registers used for the BB.
        RegSet &Used = UsedCSR[MOI->getParent()->getParent()->getNumber()];
        // Resize if it's the first time used.
        if (Used.empty())
          Used.resize(TRI.getNumRegs());
        Used.set(Reg);
      }
      // Look for any live-ins in basic blocks.
      for (const MachineBasicBlock &MBB : MF) {
        if (MBB.isLiveIn(*AliasReg)) {
          RegSet &Used = UsedCSR[MBB.getNumber()];
          // Resize if it's the first time used.
          if (Used.empty())
            Used.resize(TRI.getNumRegs());
          Used.set(Reg);
        }
      }
    }
  }
  return UsedCSR;
}

struct ShrinkWrapRemarks {
  SparseBBRegSetMap UsedCSR;
  DataflowAttributes Attr;
  RegSet VisitedRegs;
  RegScavenger *RS = nullptr;

  ShrinkWrapRemarks(MachineFunction &MF, RegScavenger *RS)
      : UsedCSR{determineCalleeSaves(MF)}, Attr{MF, UsedCSR},
        VisitedRegs{MF.getSubtarget().getRegisterInfo()->getNumRegs()}, RS{RS} {
    Attr.populate();
    for (auto &KV : UsedCSR) {
      const RegSet &Regs = KV.second;
      for (int Reg = Regs.find_first(); Reg >= 0; Reg = Regs.find_next(Reg))
        Attr.compute(Reg);
    }
  }

  bool isNewReg(unsigned Reg) {
    if (!VisitedRegs.test(Reg)) {
      VisitedRegs.set(Reg);
      return true;
    }
    return false;
  }
};

bool isUsedOnAllPaths(const MachineBasicBlock *From,
                      const MachineBasicBlock *To, RegScavenger *RS,
                      BitVector &Visited) {
  if (Visited.test(From->getNumber()))
    return true;
  Visited.set(From->getNumber());

  if (From == To)
    return true;

  bool Any =
      std::any_of(From->begin(), From->end(), [&](const MachineInstr &MI) {
        return useOrDefCSROrFI(MI, RS).first != UseDefType::None;
      });
  if (!Any)
    return false;
  bool All = std::all_of(From->successors().begin(), From->successors().end(),
                         [&](const MachineBasicBlock *Succ) {
                           return isUsedOnAllPaths(Succ, To, RS, Visited);
                         });
  return All;
}

struct ShrinkWrapState {
  const ShrinkWrapRemarks &Remarks;
  MachineBasicBlock *Save = nullptr;
  MachineBasicBlock *Restore = nullptr;
  std::pair<UseDefType, unsigned> Use;

  ShrinkWrapState(const ShrinkWrapRemarks &Remarks, MachineBasicBlock *Save,
                  MachineBasicBlock *Restore,
                  std::pair<UseDefType, unsigned> Use)
      : Remarks{Remarks}, Save{Save}, Restore{Restore}, Use{Use} {}
};

bool isBetterThan(const ShrinkWrapState &This, const ShrinkWrapState &Other) {
  if (!Other.Save || !Other.Restore || !This.Restore || !This.Save)
    return true;

  unsigned SaveFreq = MBFI->getBlockFreq(This.Save).getFrequency() /
                      MBFI->getEntryFreq();
  unsigned OtherSaveFreq = MBFI->getBlockFreq(Other.Save).getFrequency() /
                           MBFI->getEntryFreq();
  // If the new save block has a higher frequency, then it's getting worse.
  if (SaveFreq > OtherSaveFreq)
    return false;

  unsigned RestoreFreq = MBFI->getBlockFreq(This.Restore).getFrequency() /
                      MBFI->getEntryFreq();
  unsigned OtherRestoreFreq = MBFI->getBlockFreq(Other.Restore).getFrequency() /
                      MBFI->getEntryFreq();
  // If the new restore block has a higher frequency, then it's getting worse.
  if (RestoreFreq > OtherRestoreFreq)
    return false;

  BitVector Visited(MachineFunc->getNumBlockIDs());
  bool UsedOnAllPaths =
      isUsedOnAllPaths(This.Save, This.Restore, This.Remarks.RS, Visited);
  Visited.reset();
  bool OtherUsedOnAllPaths =
      isUsedOnAllPaths(Other.Save, Other.Restore, Other.Remarks.RS, Visited);
  // If it was used on all paths, but not anymore, then it's getting worse.
  if (!UsedOnAllPaths && OtherUsedOnAllPaths)
    return false;

  // If it's caused by a frame operation, then it passes.
  if (This.Use.first == UseDefType::Frame)
    return true;

  bool ANT =
      This.Remarks.Attr.ANTIN[This.Save->getNumber()].test(This.Use.second);
  bool OtherANT =
      Other.Remarks.Attr.ANTIN[Other.Save->getNumber()].test(Other.Use.second);
  // If the save anticipated the use of this register but it's not anymore,
  // then it's getting worse.
  if (!ANT && OtherANT)
    return false;

  bool AV =
      This.Remarks.Attr.AVOUT[This.Restore->getNumber()].test(This.Use.second);
  bool OtherAV = Other.Remarks.Attr.AVOUT[Other.Restore->getNumber()].test(
      Other.Use.second);
  // If the restore AVicipated the use of this register but it's not anymore,
  // then it's getting worse.
  if (!AV && OtherAV)
    return false;

  return true;
}
