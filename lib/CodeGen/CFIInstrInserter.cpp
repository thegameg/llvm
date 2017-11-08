//===------ CFIInstrInserter.cpp - Insert additional CFI instructions -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This pass verifies the incoming and outgoing CFI state of basic
/// blocks. The CFI state describes:
/// * CFA: information about offset and register set by CFI directives, valid at
/// the start and end of a basic block.
/// * Callee-saved registers: information about saving / restoring CSRs.
/// This pass checks that outgoing information of predecessors matches incoming
/// information of their successors. Then it checks if blocks have correct
/// information and inserts additional CFI instruction at their beginnings if
/// they don't. CFI instructions are inserted if basic blocks have incorrect
/// information, e.g. offset or register set by previous blocks, as a result of
/// a non-linear layout of blocks in a function.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetSubtargetInfo.h"
using namespace llvm;

namespace {
class CFIInstrInserter : public MachineFunctionPass {
public:
  static char ID;

  CFIInstrInserter() : MachineFunctionPass(ID) {
    initializeCFIInstrInserterPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {

    if (!MF.getMMI().hasDebugInfo() &&
        !MF.getFunction()->needsUnwindTableEntry())
      return false;

    MBBVector.resize(MF.getNumBlockIDs());
    calculateCFIInfo(MF);
#ifndef NDEBUG
    unsigned ErrorNum = verify(MF);
    if (ErrorNum)
      report_fatal_error("Found " + Twine(ErrorNum) +
                         " in/out CFI information errors.");
#endif
    bool insertedCFI = insertCFIInstrs(MF);
    MBBVector.clear();
    AllCSR.clear();
    return insertedCFI;
  }

private:
  struct CSRState {
    /// A register can be saved in different locations:
    enum {
      OnStack,         /// Restored using an offset from CFA.
      InRegister,      /// Restored by reading the value from another register.
      Undefined,       /// Can't be restored anymore.
      InPreviousFrame, /// No restoration needed.
    } Saved = Undefined;

    union {
      /// Offset from CFA.
      int Offset;
      /// Register where it's saved.
      unsigned Register;
    };

    bool operator==(const CSRState &Other) const {
      if (Saved != Other.Saved)
        return false;
      if (Saved == OnStack && Offset != Other.Offset)
        return false;
      if (Saved == InRegister && Register != Other.Register)
        return false;
      return true;
    }
    bool operator!=(const CSRState &Other) const { return !operator==(Other); }
  };

  struct MBBCFIInfo {
    /// Value of cfa offset valid at basic block entry.
    int IncomingCFAOffset = -1;
    /// Value of cfa offset valid at basic block exit.
    int OutgoingCFAOffset = -1;
    /// Value of cfa register valid at basic block entry.
    unsigned IncomingCFARegister = 0;
    /// Value of cfa register valid at basic block exit.
    unsigned OutgoingCFARegister = 0;
    /// State of all the CSRs at basic block entry.
    DenseMap<MCPhysReg, CSRState> IncomingCSRState;
    /// State of all the CSRs at basic block exit.
    DenseMap<MCPhysReg, CSRState> OutgoingCSRState;
    /// If CFI values for this block have already been set or not.
    bool Processed = false;
  };

  /// Contains CFI values valid at entry and exit of basic blocks.
  SmallVector<MBBCFIInfo, 4> MBBVector;

  /// All callee saved registers described by CFI directives.
  SetVector<MCPhysReg> AllCSR;

  /// Scan for all the CSRs described by CFI directives.
  void getInitialCSR(const MachineFunction &MF);

  /// Calculate CFI values valid at entry and exit for all basic blocks in a
  /// function.
  void calculateCFIInfo(const MachineFunction &MF);
  /// Calculate CFI values valid at basic block exit by checking the block for
  /// CFI instructions. Block's incoming CFI info remains the same.
  void calculateOutgoingCFIInfo(const MachineBasicBlock &MBB);
  /// Update in/out CFI values for successors of the basic block.
  void updateSuccCFIInfo(const MachineBasicBlock &MBB);

  /// Check if incoming CFI information of a basic block matches outgoing CFI
  /// information of the previous block. If it doesn't, insert CFI instruction
  /// at the beginning of the block that corrects:
  /// * the CFA calculation rule for that block.
  /// * the CSR state.
  bool insertCFIInstrs(MachineFunction &MF);
  /// Correct CFA calculation rule if needed.
  bool insertCFACFIInstrs(const MBBCFIInfo &MBBInfo,
                          const MBBCFIInfo &PrevMBBInfo, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI);
  /// Correct CSR calculation rule if needed.
  bool insertCSRCFIInstrs(const MBBCFIInfo &MBBInfo,
                          const MBBCFIInfo &PrevMBBInfo, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI);
  /// Return the cfa offset value that should be set at the beginning of a MBB
  /// if needed. The negated value is needed when creating CFI instructions that
  /// set absolute offset.
  int getCorrectCFAOffset(const MachineBasicBlock *MBB) {
    return -MBBVector[MBB->getNumber()].IncomingCFAOffset;
  }

  void report(const char *msg, const MachineBasicBlock &MBB);
  /// Go through each MBB in a function and check that outgoing offset and
  /// register of its predecessors match incoming offset and register of that
  /// MBB, as well as that incoming offset and register of its successors match
  /// outgoing offset and register of the MBB.
  unsigned verify(const MachineFunction &MF);
};
} // namespace

char CFIInstrInserter::ID = 0;
INITIALIZE_PASS(CFIInstrInserter, "cfi-instr-inserter",
                "Check CFA info and insert CFI instructions if needed", false,
                false)
FunctionPass *llvm::createCFIInstrInserter() { return new CFIInstrInserter(); }

void CFIInstrInserter::getInitialCSR(const MachineFunction &MF) {
  for (const MachineBasicBlock &MBB : MF) {
    const std::vector<MCCFIInstruction> &Instrs =
        MBB.getParent()->getFrameInstructions();
    for (const MachineInstr &MI : MBB) {
      if (MI.isCFIInstruction()) {
        unsigned CFIIndex = MI.getOperand(0).getCFIIndex();
        const MCCFIInstruction &CFI = Instrs[CFIIndex];
        switch (CFI.getOperation()) {
        case MCCFIInstruction::OpSameValue:
        case MCCFIInstruction::OpOffset:
        case MCCFIInstruction::OpRelOffset:
        case MCCFIInstruction::OpRestore:
        case MCCFIInstruction::OpUndefined:
        case MCCFIInstruction::OpRegister: {
          AllCSR.insert(CFI.getRegister());
          break;
        }
        default:
          break;
        }
      }
    }
  }
}

void CFIInstrInserter::calculateCFIInfo(const MachineFunction &MF) {
  // Initial CFA offset value i.e. the one valid at the beginning of the
  // function.
  int InitialOffset =
      MF.getSubtarget().getFrameLowering()->getInitialCFAOffset(MF);
  // Initial CFA register value i.e. the one valid at the beginning of the
  // function.
  unsigned InitialRegister =
      MF.getSubtarget().getFrameLowering()->getInitialCFARegister(MF);

  // Initial CSR that are described in the function.
  getInitialCSR(MF);

  // Initialize MBBMap.
  for (const MachineBasicBlock &MBB : MF) {
    MBBCFIInfo &MBBInfo = MBBVector[MBB.getNumber()];
    MBBInfo.IncomingCFAOffset = InitialOffset;
    MBBInfo.OutgoingCFAOffset = InitialOffset;
    MBBInfo.IncomingCFARegister = InitialRegister;
    MBBInfo.OutgoingCFARegister = InitialRegister;
    for (auto *Map : {&MBBInfo.OutgoingCSRState, &MBBInfo.IncomingCSRState}) {
      Map->reserve(AllCSR.size());
      for (MCPhysReg Reg : AllCSR)
        (*Map)[Reg].Saved = CSRState::Undefined;
    }
  }

  // Set in/out cfi info for all blocks in the function. This traversal is based
  // on the assumption that the first block in the function is the entry block
  // i.e. that it has initial cfa offset and register values as incoming CFA
  // information.
  for (const MachineBasicBlock &MBB : MF) {
    if (MBBVector[MBB.getNumber()].Processed)
      continue;
    calculateOutgoingCFIInfo(MBB);
    updateSuccCFIInfo(MBB);
  }
}

void CFIInstrInserter::calculateOutgoingCFIInfo(const MachineBasicBlock &MBB) {
  MBBCFIInfo &MBBInfo = MBBVector[MBB.getNumber()];
  // Outgoing cfa offset set by the block.
  int SetOffset = MBBInfo.IncomingCFAOffset;
  // Outgoing cfa register set by the block.
  unsigned SetRegister = MBBInfo.IncomingCFARegister;
  const std::vector<MCCFIInstruction> &Instrs =
      MBB.getParent()->getFrameInstructions();

  MBBInfo.OutgoingCSRState = MBBInfo.IncomingCSRState;

  // Determine cfa offset and register set by the block.
  for (const MachineInstr &MI : MBB) {
    if (MI.isCFIInstruction()) {
      unsigned CFIIndex = MI.getOperand(0).getCFIIndex();
      const MCCFIInstruction &CFI = Instrs[CFIIndex];
      switch (CFI.getOperation()) {
      case MCCFIInstruction::OpDefCfaRegister: {
        SetRegister = CFI.getRegister();
        break;
      }
      case MCCFIInstruction::OpDefCfaOffset: {
        SetOffset = CFI.getOffset();
        break;
      }
      case MCCFIInstruction::OpAdjustCfaOffset: {
        SetOffset += CFI.getOffset();
        break;
      }
      case MCCFIInstruction::OpDefCfa: {
        SetRegister = CFI.getRegister();
        SetOffset = CFI.getOffset();
        break;
      }
      case MCCFIInstruction::OpSameValue: {
        MBBInfo.OutgoingCSRState[CFI.getRegister()].Saved =
            CSRState::InPreviousFrame;
        break;
      }
      case MCCFIInstruction::OpOffset: {
        CSRState &Reg = MBBInfo.OutgoingCSRState[CFI.getRegister()];
        Reg.Saved = CSRState::OnStack;
        Reg.Offset = CFI.getOffset();
        break;
      }
      case MCCFIInstruction::OpUndefined: {
        MBBInfo.OutgoingCSRState[CFI.getRegister()].Saved =
            CSRState::Undefined;
        break;
      }
      case MCCFIInstruction::OpRegister: {
        CSRState &Reg = MBBInfo.OutgoingCSRState[CFI.getRegister()];
        Reg.Saved = CSRState::InRegister;
        Reg.Register = CFI.getRegister2();
        break;
      }
      case MCCFIInstruction::OpRelOffset: {
        CSRState &Reg = MBBInfo.OutgoingCSRState[CFI.getRegister()];
        Reg.Saved = CSRState::OnStack;
        Reg.Offset = CFI.getOffset() + SetRegister;
        break;
      }
      case MCCFIInstruction::OpRestore: {
        MBBInfo.OutgoingCSRState[CFI.getRegister()].Saved =
            CSRState::Undefined;
        break;
      }
      default: {
        DEBUG(dbgs() << "Ignored CFI directive: "; MI.print(dbgs());
              dbgs() << '\n');
        break;
      }
      }
    }
  }

  MBBInfo.Processed = true;

  // Update outgoing CFA info.
  MBBInfo.OutgoingCFAOffset = SetOffset;
  MBBInfo.OutgoingCFARegister = SetRegister;
}

void CFIInstrInserter::updateSuccCFIInfo(const MachineBasicBlock &MBB) {
  MBBCFIInfo &MBBInfo = MBBVector[MBB.getNumber()];
  for (const MachineBasicBlock *Succ : MBB.successors()) {
    MBBCFIInfo &SuccInfo = MBBVector[Succ->getNumber()];
    if (SuccInfo.Processed)
      continue;
    SuccInfo.IncomingCFAOffset = MBBInfo.OutgoingCFAOffset;
    SuccInfo.IncomingCFARegister = MBBInfo.OutgoingCFARegister;
    SuccInfo.IncomingCSRState = MBBInfo.OutgoingCSRState;

    calculateOutgoingCFIInfo(*Succ);
    updateSuccCFIInfo(*Succ);
  }
}

bool CFIInstrInserter::insertCFIInstrs(MachineFunction &MF) {
  const MBBCFIInfo *PrevMBBInfo = &MBBVector[MF.front().getNumber()];
  bool InsertedCFIInstr = false;

  for (MachineBasicBlock &MBB : MF) {
    // Skip the first MBB in a function
    if (MBB.getNumber() == MF.front().getNumber())
      continue;

    const MBBCFIInfo &MBBInfo = MBBVector[MBB.getNumber()];
    auto MBBI = MBB.begin();

    InsertedCFIInstr |= insertCFACFIInstrs(MBBInfo, *PrevMBBInfo, MBB, MBBI);
    InsertedCFIInstr |= insertCSRCFIInstrs(MBBInfo, *PrevMBBInfo, MBB, MBBI);

    PrevMBBInfo = &MBBInfo;
  }
  return InsertedCFIInstr;
}

bool CFIInstrInserter::insertCFACFIInstrs(const MBBCFIInfo &MBBInfo,
                                          const MBBCFIInfo &PrevMBBInfo,
                                          MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator &MBBI) {
  MachineFunction &MF = *MBB.getParent();
  DebugLoc DL = MBB.findDebugLoc(MBBI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  if (PrevMBBInfo.OutgoingCFAOffset != MBBInfo.IncomingCFAOffset) {
    // If both outgoing offset and register of a previous block don't match
    // incoming offset and register of this block, add a def_cfa instruction
    // with the correct offset and register for this block.
    if (PrevMBBInfo.OutgoingCFARegister != MBBInfo.IncomingCFARegister) {
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfa(
          nullptr, MBBInfo.IncomingCFARegister, getCorrectCFAOffset(&MBB)));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
      // If outgoing offset of a previous block doesn't match incoming offset
      // of this block, add a def_cfa_offset instruction with the correct
      // offset for this block.
    } else {
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfaOffset(
          nullptr, getCorrectCFAOffset(&MBB)));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
    return true;
    // If outgoing register of a previous block doesn't match incoming
    // register of this block, add a def_cfa_register instruction with the
    // correct register for this block.
  } else if (PrevMBBInfo.OutgoingCFARegister != MBBInfo.IncomingCFARegister) {
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
        nullptr, MBBInfo.IncomingCFARegister));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    return true;
  }
  return false;
}

bool CFIInstrInserter::insertCSRCFIInstrs(const MBBCFIInfo &MBBInfo,
                                          const MBBCFIInfo &PrevMBBInfo,
                                          MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator &MBBI) {
  MachineFunction &MF = *MBB.getParent();
  DebugLoc DL = MBB.findDebugLoc(MBBI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool InsertedCFIInstr = false;

  for (MCPhysReg Reg : AllCSR) {
    const CSRState &Prev = PrevMBBInfo.OutgoingCSRState.find(Reg)->second;
    const CSRState &Curr = MBBInfo.IncomingCSRState.find(Reg)->second;
    if (Prev == Curr)
      continue;
    MCCFIInstruction MCCFI = [&] {
      switch (Curr.Saved) {
      case CSRState::Undefined:
        return MCCFIInstruction::createUndefined(nullptr, Reg);
      case CSRState::InPreviousFrame:
        return MCCFIInstruction::createSameValue(nullptr, Reg);
      case CSRState::OnStack:
        return MCCFIInstruction::createOffset(nullptr, Reg, Curr.Offset);
      case CSRState::InRegister:
        return MCCFIInstruction::createRegister(nullptr, Reg, Curr.Register);
      }
    }();

    unsigned CFIIndex = MF.addFrameInst(MCCFI);
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    InsertedCFIInstr = true;
  }

  return InsertedCFIInstr;
}

void CFIInstrInserter::report(const char *msg, const MachineBasicBlock &MBB) {
  errs() << '\n';
  errs() << "*** " << msg << " ***\n"
         << "- function:    " << MBB.getParent()->getName() << "\n";
  errs() << "- basic block: BB#" << MBB.getNumber() << ' ' << MBB.getName()
         << " (" << (const void *)&MBB << ')';
  errs() << '\n';
}

unsigned CFIInstrInserter::verify(const MachineFunction &MF) {
  unsigned ErrorNum = 0;
  auto MapsAreDifferent = [&](const DenseMap<MCPhysReg, CSRState> &A,
                              const DenseMap<MCPhysReg, CSRState> &B) {
    for (MCPhysReg Reg : AllCSR)
      if (A.lookup(Reg) != B.lookup(Reg))
        return true;
    return false;
  };
  for (const MachineBasicBlock &CurrMBB : MF) {
    const MBBCFIInfo &CurrMBBInfo = MBBVector[CurrMBB.getNumber()];
    for (const MachineBasicBlock *Pred : CurrMBB.predecessors()) {
      const MBBCFIInfo &PredMBBInfo = MBBVector[Pred->getNumber()];
      // Check that outgoing offset values of predecessors match the incoming
      // offset value of CurrMBB
      if (PredMBBInfo.OutgoingCFAOffset != CurrMBBInfo.IncomingCFAOffset) {
        report("The outgoing offset of a predecessor is inconsistent.",
               CurrMBB);
        errs() << "Predecessor BB#" << Pred->getNumber()
               << " has outgoing offset (" << PredMBBInfo.OutgoingCFAOffset
               << "), while BB#" << CurrMBB.getNumber()
               << " has incoming offset (" << CurrMBBInfo.IncomingCFAOffset
               << ").\n";
        ErrorNum++;
      }
      // Check that outgoing register values of predecessors match the incoming
      // register value of CurrMBB
      if (PredMBBInfo.OutgoingCFARegister != CurrMBBInfo.IncomingCFARegister) {
        report("The outgoing register of a predecessor is inconsistent.",
               CurrMBB);
        errs() << "Predecessor BB#" << Pred->getNumber()
               << " has outgoing register (" << PredMBBInfo.OutgoingCFARegister
               << "), while BB#" << CurrMBB.getNumber()
               << " has incoming register (" << CurrMBBInfo.IncomingCFARegister
               << ").\n";
        ErrorNum++;
      }

      if (MapsAreDifferent(PredMBBInfo.OutgoingCSRState,
                           CurrMBBInfo.IncomingCSRState)) {
        report("The outgoing CSR info of a predecessor is inconsistent.",
               CurrMBB);
        errs() << "Predecessor BB#" << Pred->getNumber() << ".\n";
        ErrorNum++;
      }
    }

    for (const MachineBasicBlock *Succ : CurrMBB.successors()) {
      const MBBCFIInfo &SuccMBBInfo = MBBVector[Succ->getNumber()];
      // Check that incoming offset values of successors match the outgoing
      // offset value of CurrMBB
      if (SuccMBBInfo.IncomingCFAOffset != CurrMBBInfo.OutgoingCFAOffset) {
        report("The incoming offset of a successor is inconsistent.", CurrMBB);
        errs() << "Successor BB#" << Succ->getNumber()
               << " has incoming offset (" << SuccMBBInfo.IncomingCFAOffset
               << "), while BB#" << CurrMBB.getNumber()
               << " has outgoing offset (" << CurrMBBInfo.OutgoingCFAOffset
               << ").\n";
        ErrorNum++;
      }
      // Check that incoming register values of successors match the outgoing
      // register value of CurrMBB
      if (SuccMBBInfo.IncomingCFARegister != CurrMBBInfo.OutgoingCFARegister) {
        report("The incoming register of a successor is inconsistent.",
               CurrMBB);
        errs() << "Successor BB#" << Succ->getNumber()
               << " has incoming register (" << SuccMBBInfo.IncomingCFARegister
               << "), while BB#" << CurrMBB.getNumber()
               << " has outgoing register (" << CurrMBBInfo.OutgoingCFARegister
               << ").\n";
        ErrorNum++;
      }

      if (MapsAreDifferent(SuccMBBInfo.IncomingCSRState,
                           CurrMBBInfo.OutgoingCSRState)) {
        report("The incoming CSR info of a successor is inconsistent.",
               CurrMBB);
        errs() << "Successor BB#" << Succ->getNumber() << ".\n";
        ErrorNum++;
      }
    }
  }
  return ErrorNum;
}
