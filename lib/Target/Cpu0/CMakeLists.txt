set(LLVM_TARGET_DEFINITIONS Cpu0.td)

add_public_tablegen_target(Cpu0CommonTableGen)

add_llvm_target(Cpu0CodeGen
  Cpu0TargetMachine.cpp
  )
