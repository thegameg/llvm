; XFAIL: *
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; RUN: llc -verify-machineinstrs -mtriple=powerpc64le-unknown-linux-gnu -O2 \
; RUN:   -ppc-asm-full-reg-names -mcpu=pwr8 < %s | FileCheck %s \
; RUN:  --implicit-check-not cmpw --implicit-check-not cmpd --implicit-check-not cmpl
; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
@glob = common local_unnamed_addr global i8 0, align 1

define signext i32 @test_ineuc(i8 zeroext %a, i8 zeroext %b) {
; CHECK-LABEL: test_ineuc:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    xor r3, r3, r4
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, %b
  %conv2 = zext i1 %cmp to i32
  ret i32 %conv2
}

define signext i32 @test_ineuc_sext(i8 zeroext %a, i8 zeroext %b) {
; CHECK-LABEL: test_ineuc_sext:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    xor r3, r3, r4
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, %b
  %sub = sext i1 %cmp to i32
  ret i32 %sub
}

define signext i32 @test_ineuc_z(i8 zeroext %a) {
; CHECK-LABEL: test_ineuc_z:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, 0
  %conv1 = zext i1 %cmp to i32
  ret i32 %conv1
}

define signext i32 @test_ineuc_sext_z(i8 zeroext %a) {
; CHECK-LABEL: test_ineuc_sext_z:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, 0
  %sub = sext i1 %cmp to i32
  ret i32 %sub
}

define void @test_ineuc_store(i8 zeroext %a, i8 zeroext %b) {
; CHECK-LABEL: test_ineuc_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    xor r3, r3, r4
; CHECK-NEXT:    addis r5, r2, .LC0@toc@ha
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    ld r4, .LC0@toc@l(r5)
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    stb r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, %b
  %conv3 = zext i1 %cmp to i8
  store i8 %conv3, i8* @glob, align 1
  ret void
}

define void @test_ineuc_sext_store(i8 zeroext %a, i8 zeroext %b) {
; CHECK-LABEL: test_ineuc_sext_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    xor r3, r3, r4
; CHECK-NEXT:    addis r5, r2, .LC0@toc@ha
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    ld r4, .LC0@toc@l(r5)
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    stb r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, %b
  %conv3 = sext i1 %cmp to i8
  store i8 %conv3, i8* @glob, align 1
  ret void
}

define void @test_ineuc_z_store(i8 zeroext %a) {
; CHECK-LABEL: test_ineuc_z_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r4, r2, .LC0@toc@ha
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    ld r4, .LC0@toc@l(r4)
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    stb r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, 0
  %conv2 = zext i1 %cmp to i8
  store i8 %conv2, i8* @glob, align 1
  ret void
}

define void @test_ineuc_sext_z_store(i8 zeroext %a) {
; CHECK-LABEL: test_ineuc_sext_z_store:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    addis r4, r2, .LC0@toc@ha
; CHECK-NEXT:    cntlzw r3, r3
; CHECK-NEXT:    srwi r3, r3, 5
; CHECK-NEXT:    ld r4, .LC0@toc@l(r4)
; CHECK-NEXT:    xori r3, r3, 1
; CHECK-NEXT:    neg r3, r3
; CHECK-NEXT:    stb r3, 0(r4)
; CHECK-NEXT:    blr
entry:
  %cmp = icmp ne i8 %a, 0
  %conv2 = sext i1 %cmp to i8
  store i8 %conv2, i8* @glob, align 1
  ret void
}
