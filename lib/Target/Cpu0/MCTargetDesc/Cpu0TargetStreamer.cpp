//===-- Cpu0TargetStreamer.cpp - Cpu0 Register Information -== ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cpu0 implementation of the MCTargetStreamer class.
//
//===----------------------------------------------------------------------===//

#include "Cpu0TargetStreamer.h"

using namespace llvm;

Cpu0TargetStreamer::Cpu0TargetStreamer(MCStreamer &S) : MCTargetStreamer{S} {}

Cpu0TargetStreamer::~Cpu0TargetStreamer() {}

Cpu0TargetAsmStreamer::Cpu0TargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : Cpu0TargetStreamer{S}, OS_{OS} {}

Cpu0TargetELFStreamer::Cpu0TargetELFStreamer(MCStreamer &S,
                                             const MCSubtargetInfo &STI)
    : Cpu0TargetStreamer{S}, STI_{STI} {}
