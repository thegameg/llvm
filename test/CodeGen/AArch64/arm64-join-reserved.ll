; RUN: llc < %s -verify-machineinstrs | FileCheck %s
; XFAIL: *
; FIXME: ShrinkWrap2: This test fails with shrink-wrapping enabled because we
; don't spill x29, so we merge the store of x30 with wrz.
target triple = "arm64-apple-macosx10"

; Make sure that a store to [sp] addresses off sp directly.
; A move isn't necessary.
; <rdar://problem/11492712>
; CHECK-LABEL: g:
; CHECK: str xzr, [sp]
; CHECK: bl
; CHECK: ret
define void @g() nounwind ssp {
entry:
  tail call void (i32, ...) @f(i32 0, i32 0) nounwind
  ret void
}

declare void @f(i32, ...)
