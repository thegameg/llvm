; RUN: llc -march=cpu0 < %s -o - | FileCheck %s

define i32 @foo(i32 %a, i32 %b) nounwind {
entry:
  %sub = add i32 %a, -20
  %sub1 = sub i32 %sub, %b
  ret i32 %sub1
; CHECK: foo:
; CHECK: add $[[REG1:r[0-9]+]], $r4, -20
; CHECK: sub $[[REG2:r[0-9]+]], $[[REG1]], $r5
; CHECK: ret
}
