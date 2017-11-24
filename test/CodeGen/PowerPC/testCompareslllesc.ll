; XFAIL: *
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; RUN: llc -verify-machineinstrs -mtriple=powerpc64le-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py

@glob = common local_unnamed_addr global i8 0, align 1

define i64 @test_lllesc(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: test_lllesc:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sub r3, r4, r3
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sle i8 %a, %b
  %conv3 = zext i1 %cmp to i64
  ret i64 %conv3
}

define i64 @test_lllesc_sext(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: test_lllesc_sext:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    sub r3, r4, r3
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    addi r3, r3, -1
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sle i8 %a, %b
  %conv3 = sext i1 %cmp to i64
  ret i64 %conv3
}

define void @test_lllesc_store(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: test_lllesc_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r5, r2, .LC0@toc@ha
; CHECK-NEXT:    sub r3, r4, r3
; CHECK-NEXT:    ld r12, .LC0@toc@l(r5)
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    stb r3, 0(r12)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sle i8 %a, %b
  %conv3 = zext i1 %cmp to i8
  store i8 %conv3, i8* @glob, align 1
  ret void
}

define void @test_lllesc_sext_store(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: test_lllesc_sext_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r5, r2, .LC0@toc@ha
; CHECK-NEXT:    sub r3, r4, r3
; CHECK-NEXT:    ld r12, .LC0@toc@l(r5)
; CHECK-NEXT:    rldicl r3, r3, 1, 63
; CHECK-NEXT:    addi r3, r3, -1
; CHECK-NEXT:    stb r3, 0(r12)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp sle i8 %a, %b
  %conv3 = sext i1 %cmp to i8
  store i8 %conv3, i8* @glob, align 1
  ret void
}
