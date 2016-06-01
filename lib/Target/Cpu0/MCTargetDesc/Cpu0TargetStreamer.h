//===-- Cpu0TargetStreamer.h - Cpu0 Register Information -== --------------===//
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

#ifndef LLVM_LIB_TARGET_CPU0_CPU0TARGETSTREAMER_H
#define LLVM_LIB_TARGET_CPU0_CPU0TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/FormattedStream.h"

namespace llvm {

class Cpu0TargetStreamer : public MCTargetStreamer {
public:
  Cpu0TargetStreamer(MCStreamer &S);

  virtual ~Cpu0TargetStreamer();

private:
};

class Cpu0TargetAsmStreamer : public Cpu0TargetStreamer {
public:
  Cpu0TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

private:
  formatted_raw_ostream &OS_;
};

class Cpu0TargetELFStreamer : public Cpu0TargetStreamer {
public:
  Cpu0TargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

private:
  const MCSubtargetInfo &STI_;
};

} // namespace llvm

#endif /* !LLVM_LIB_TARGET_CPU0_CPU0TARGETSTREAMER_H */
