; XFAIL: *
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; RUN: llc -verify-machineinstrs -mtriple=powerpc64le-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py

@glob = common local_unnamed_addr global i32 0, align 4

; Function Attrs: norecurse nounwind readnone
define signext i32 @test_iltsi(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: test_iltsi:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sub [[REG:r[0-9]+]], r3, r4
; CHECK-NEXT:    rldicl r3, [[REG]], 1, 63
; CHECK-NEXT:    blr
entry:
  %cmp = icmp slt i32 %a, %b
  %conv = zext i1 %cmp to i32
  ret i32 %conv
}

; Function Attrs: norecurse nounwind readnone
define signext i32 @test_iltsi_sext(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: test_iltsi_sext:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sub [[REG:r[0-9]+]], r3, r4
; CHECK-NEXT:    sradi r3, [[REG]], 63
; CHECK-NEXT:    blr
entry:
  %cmp = icmp slt i32 %a, %b
  %sub = sext i1 %cmp to i32
  ret i32 %sub
}

; Function Attrs: norecurse nounwind readnone
define signext i32 @test_iltsi_sext_z(i32 signext %a) {
; CHECK-LABEL: test_iltsi_sext_z:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    srawi r3, r3, 31
; CHECK-NEXT:    blr
entry:
  %cmp = icmp slt i32 %a, 0
  %sub = sext i1 %cmp to i32
  ret i32 %sub
}

; Function Attrs: norecurse nounwind
define void @test_iltsi_store(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: test_iltsi_store:
; CHECK:       # %bb.0: # %entry
; CHECK:         sub [[REG:r[0-9]+]], r3, r4
; CHECK:         rldicl {{r[0-9]+}}, [[REG]], 1, 63
entry:
  %cmp = icmp slt i32 %a, %b
  %conv = zext i1 %cmp to i32
  store i32 %conv, i32* @glob, align 4
  ret void
}

; Function Attrs: norecurse nounwind
define void @test_iltsi_sext_store(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: test_iltsi_sext_store:
; CHECK:       # %bb.0: # %entry
; CHECK:         sub [[REG:r[0-9]+]], r3, r4
; CHECK:         sradi {{r[0-9]+}}, [[REG]], 63
entry:
  %cmp = icmp slt i32 %a, %b
  %sub = sext i1 %cmp to i32
  store i32 %sub, i32* @glob, align 4
  ret void
}

; Function Attrs: norecurse nounwind
define void @test_iltsi_sext_z_store(i32 signext %a) {
; CHECK-LABEL: test_iltsi_sext_z_store:
; CHECK:    srawi {{r[0-9]+}}, r3, 31
; CHECK:    blr
entry:
  %cmp = icmp slt i32 %a, 0
  %sub = sext i1 %cmp to i32
  store i32 %sub, i32* @glob, align 4
  ret void
}
