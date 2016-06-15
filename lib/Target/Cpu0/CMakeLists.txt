set(LLVM_TARGET_DEFINITIONS Cpu0.td)

tablegen(LLVM Cpu0GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM Cpu0GenCallingConv.inc -gen-callingconv)
tablegen(LLVM Cpu0GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM Cpu0GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM Cpu0GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM Cpu0GenSubtargetInfo.inc -gen-subtarget)
add_public_tablegen_target(Cpu0CommonTableGen)

add_llvm_target(Cpu0CodeGen
  Cpu0AsmPrinter.cpp
  Cpu0FrameLowering.cpp
  Cpu0ISelDAGToDAG.cpp
  Cpu0ISelLowering.cpp
  Cpu0InstrInfo.cpp
  Cpu0MCInstLower.cpp
  Cpu0RegisterInfo.cpp
  Cpu0RegisterInfo.cpp
  Cpu0Subtarget.cpp
  Cpu0TargetMachine.cpp
  Cpu0TargetObjectFile.cpp
  )

add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
