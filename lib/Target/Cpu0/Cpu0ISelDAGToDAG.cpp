//===-- Cpu0ISelDAGToDAG.cpp - A Dag to Dag Inst Selector for Cpu0 --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the Cpu0 target.
//
//===----------------------------------------------------------------------===//

#include "Cpu0ISelDAGToDAG.h"
using namespace llvm;

#define DEBUG_TYPE "Cpu0-isel"

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Cpu0DAGToDAGISel - Cpu0 specific code to select Cpu0 machine
// instructions for SelectionDAG operations.
//===----------------------------------------------------------------------===//

/// Select instructions not customized! Used for
/// expanded, promoted and normal instructions
void Cpu0DAGToDAGISel::Select(SDNode *Node) {
  // Dump information about the Node being selected
  DEBUG(errs() << "Selecting: "; Node->dump(CurDAG); errs() << "\n");

  // Select the default instruction
  SelectCode(Node);
}
