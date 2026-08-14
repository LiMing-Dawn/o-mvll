;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; Verify that barrier instructions (calls, volatile loads/stores, atomics) are
; never moved relative to adjacent instructions. The pass must leave barriers
; exactly where they are and only shuffle instructions within the slots between
; them.

; RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o - | FileCheck %s

declare void @extern_func()

; %before must come before the call, %after must come after it.
; CHECK-LABEL: define i32 @with_call(
; CHECK: %before = add i32
; CHECK: call void @extern_func()
; CHECK: %after = mul i32

define i32 @with_call(i32 %x) {
entry:
  %before = add i32 %x, 1
  call void @extern_func()
  %after = mul i32 %x, 2
  ret i32 %after
}

; Volatile load must not be moved across the surrounding arithmetic.
; CHECK-LABEL: define i32 @with_volatile(
; CHECK: %pre = add i32
; CHECK: %v = load volatile i32
; CHECK: %post = sub i32

define i32 @with_volatile(i32 %x, ptr %p) {
entry:
  %pre  = add i32 %x, 1
  %v    = load volatile i32, ptr %p
  %post = sub i32 %v, 1
  ret i32 %post
}

; Atomic fence must not be reordered relative to flanking instructions.
; CHECK-LABEL: define void @with_fence(
; CHECK: store i32
; CHECK: fence seq_cst
; CHECK: store i32

define void @with_fence(ptr %p, ptr %q) {
entry:
  store i32 1, ptr %p
  fence seq_cst
  store i32 2, ptr %q
  ret void
}
