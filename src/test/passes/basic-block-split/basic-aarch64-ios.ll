;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o - | FileCheck --check-prefixes=CHECK %s

; The entry block is never split. The 'work' block has four non-terminator
; instructions, so it is split down the middle: %w1/%w2 stay in 'work', which
; branches to a new block holding %w3/%w4 and the terminator.

define i32 @split_me(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @split_me(i32 %a, i32 %b)
; CHECK:       entry:
; CHECK-NEXT:    br label %work
; CHECK:       work:
; CHECK-NEXT:    %w1 = add i32 %a, %b
; CHECK-NEXT:    %w2 = mul i32 %w1, %a
; CHECK-NEXT:    br label %[[SPLIT:.*]]
; CHECK:       [[SPLIT]]:
; CHECK-NEXT:    %w3 = sub i32 %w2, %b
; CHECK-NEXT:    %w4 = xor i32 %w3, %w1
; CHECK-NEXT:    ret i32 %w4
;
entry:
  br label %work

work:
  %w1 = add i32 %a, %b
  %w2 = mul i32 %w1, %a
  %w3 = sub i32 %w2, %b
  %w4 = xor i32 %w3, %w1
  ret i32 %w4
}
