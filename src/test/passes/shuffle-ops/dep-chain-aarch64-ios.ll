;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; Verify that the pass produces valid IR (compilation succeeds) and that the
; data-dependency chain %a -> %b -> %c is always preserved in the output,
; regardless of the random ordering chosen for the independent instruction %d.

; RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o - | FileCheck %s

; RUN: env OMVLL_CONFIG=%S/config_disabled.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o - | FileCheck --check-prefix=DISABLED %s

; With shuffling enabled the chain %a -> %b -> %c must appear in this relative
; order (gaps allowed for independent instructions such as %d).
; CHECK-LABEL: define i32 @dep_chain(
; CHECK: %a = add i32
; CHECK: %b = mul i32 %a
; CHECK: %c = sub i32 %b

; With shuffling disabled the block must be unchanged — %d appears between %b
; and %c in the original IR.
; DISABLED-LABEL: define i32 @dep_chain(
; DISABLED: %a = add i32
; DISABLED-NEXT: %b = mul i32 %a
; DISABLED-NEXT: %c = sub i32 %b
; DISABLED-NEXT: %d = add i32

define i32 @dep_chain(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = mul i32 %a, 2
  %c = sub i32 %b, 1
  %d = add i32 %x, 100
  ret i32 %c
}
