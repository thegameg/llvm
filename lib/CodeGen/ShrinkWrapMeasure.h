#ifndef LLVM_CODEGEN_SHRINKWRAPMEASURE
#define LLVM_CODEGEN_SHRINKWRAPMEASURE

#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"

namespace llvm {

// FIXME: ShrinkWrap2: Maybe not the best idea...
template <typename ShrinkWrapPass> struct Measure {
  const ShrinkWrapPass &Pass;
  const MachineFunction &MF;

  unsigned getAverageCostOfFunction() const {
    unsigned Cost = 0;
    for (const MachineBasicBlock &MBB : MF) {
      unsigned BlockCost = 1000;
      unsigned Extra = 0;
      Extra += Pass.isRestore(MBB) * 5;
      Extra += Pass.isSave(MBB) * 5;
      if (Pass.isRestore(MBB) && Pass.isSave(MBB))
        Extra = 10;
      else if (Pass.isRestore(MBB) || Pass.isSave(MBB))
        Extra = 5;
      if (Extra)
        BlockCost *= Extra;

      BlockCost *= double(Pass.MBFI->getBlockFreq(&MBB).getFrequency()) /
                   Pass.MBFI->getEntryFreq() * 100;
      Cost += BlockCost;
    }
    Cost /= MF.size();
    return Cost;
  }

  unsigned getSaveRestoreCostOfFunction() const {
    unsigned Cost = 0;
    // dbgs() << MF.getName() << '\n';
    for (const MachineBasicBlock &MBB : MF) {
      unsigned BlockCost = Pass.isRestore(MBB) + Pass.isSave(MBB);
      // dbgs() << "#" << MBB.getNumber() << " : "
      //       << uint64_t(double(Pass.MBFI->getBlockFreq(&MBB).getFrequency())
      //       / Pass.MBFI->getEntryFreq() * 100) << '\n';
      BlockCost *= double(Pass.MBFI->getBlockFreq(&MBB).getFrequency()) /
                   Pass.MBFI->getEntryFreq() * 100;
      Cost += BlockCost;
    }
    return Cost;
  }

  Measure(const ShrinkWrapPass &Pass, const MachineFunction &MF)
      : Pass{Pass}, MF{MF} {}

  ~Measure() {
    auto Cost = getSaveRestoreCostOfFunction();
    DebugLoc DL = [&] {
      if (!MF.size())
        return DebugLoc{};

      auto &F = MF.front();
      if (!F.size())
        return DebugLoc{};

      auto &I = F.front();
      return I.getDebugLoc();
    }();
    MachineOptimizationRemarkAnalysis R("shrink-wrap", "shrink-wrapped", DL,
                                        &MF.front());
    R << ore::NV("ShrinkWrapCost", Cost);
    Pass.ORE->emit(R);
  }
};
}
#endif
