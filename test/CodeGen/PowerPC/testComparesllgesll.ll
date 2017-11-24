; XFAIL: *
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; RUN: llc -verify-machineinstrs -mtriple=powerpc64le-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
@glob = common local_unnamed_addr global i64 0, align 8

define i64 @test_llgesll(i64 %a, i64 %b) {
; CHECK-LABEL: test_llgesll:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sradi r5, r3, 63
; CHECK-NEXT:    rldicl r6, r4, 1, 63
; CHECK-NEXT:    subfc r3, r4, r3
; CHECK-NEXT:    adde r3, r5, r6
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sge i64 %a, %b
  %conv1 = zext i1 %cmp to i64
  ret i64 %conv1
}

define i64 @test_llgesll_sext(i64 %a, i64 %b) {
; CHECK-LABEL: test_llgesll_sext:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sradi r5, r3, 63
; CHECK-NEXT:    rldicl r6, r4, 1, 63
; CHECK-NEXT:    subfc r3, r4, r3
; CHECK-NEXT:    adde r3, r5, r6
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sge i64 %a, %b
  %conv1 = sext i1 %cmp to i64
  ret i64 %conv1
}

define i64 @test_llgesll_z(i64 %a) {
; CHECK-LABEL: test_llgesll_z:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sgt i64 %a, -1
  %conv1 = zext i1 %cmp to i64
  ret i64 %conv1
}

define i64 @test_llgesll_sext_z(i64 %a) {
; CHECK-LABEL: test_llgesll_sext_z:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sradi r3, r3, 63
; CHECK-NEXT:    not r3, r3
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sgt i64 %a, -1
  %conv1 = sext i1 %cmp to i64
  ret i64 %conv1
}

define void @test_llgesll_store(i64 %a, i64 %b) {
; CHECK-LABEL: test_llgesll_store:
; CHECK:       # %bb.0: # %entry
; CHECK:    sradi r6, r3, 63
; CHECK:    subfc r3, r4, r3
; CHECK:    rldicl r3, r4, 1, 63
; CHECK:    adde r3, r6, r3
; CHECK:    std r3,
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sge i64 %a, %b
  %conv1 = zext i1 %cmp to i64
  store i64 %conv1, i64* @glob, align 8
  ret void
}

define void @test_llgesll_sext_store(i64 %a, i64 %b) {
; CHECK-LABEL: test_llgesll_sext_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sradi r6, r3, 63
; CHECK-NEXT:    addis r5, r2, .LC0@toc@ha
; CHECK-NEXT:    subfc r3, r4, r3
; CHECK-NEXT:    rldicl r3, r4, 1, 63
; CHECK-NEXT:    ld r4, .LC0@toc@l(r5)
; CHECK-NEXT:    adde r3, r6, r3
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    std r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sge i64 %a, %b
  %conv1 = sext i1 %cmp to i64
  store i64 %conv1, i64* @glob, align 8
  ret void
}

define void @test_llgesll_z_store(i64 %a) {
; CHECK-LABEL: test_llgesll_z_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r4, r2, .LC0@toc@ha
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    ld r4, .LC0@toc@l(r4)
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    std r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sgt i64 %a, -1
  %conv1 = zext i1 %cmp to i64
  store i64 %conv1, i64* @glob, align 8
  ret void
}

define void @test_llgesll_sext_z_store(i64 %a) {
; CHECK-LABEL: test_llgesll_sext_z_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r4, r2, .LC0@toc@ha
; CHECK-NEXT:    sradi r3, r3, 63
; CHECK-NEXT:    ld r4, .LC0@toc@l(r4)
; CHECK-NEXT:    not r3, r3
; CHECK-NEXT:    std r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sgt i64 %a, -1
  %conv1 = sext i1 %cmp to i64
  store i64 %conv1, i64* @glob, align 8
  ret void
}
