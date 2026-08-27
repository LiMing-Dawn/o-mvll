;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; The pass moves the body of a function into a clone and turns the original
; into a trampoline. Function::deleteBody() resets the linkage to external on
; the way, so the trampoline has to be given the original linkage back.

; RUN: env OMVLL_CONFIG=%S/config_all.py clang -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O1 -S -emit-llvm %s -o - | FileCheck %s

define linkonce_odr hidden i32 @linkonce_fn(i32 %x) noinline {
entry:
  %m = mul i32 %x, 7
  ret i32 %m
}

define weak_odr i32 @weak_fn(i32 %x) noinline {
entry:
  %m = mul i32 %x, 11
  ret i32 %m
}

define internal i32 @internal_fn(i32 %x) noinline {
entry:
  %m = mul i32 %x, 13
  ret i32 %m
}

define i32 @use(i32 %x) {
entry:
  %a = call i32 @linkonce_fn(i32 %x)
  %b = call i32 @weak_fn(i32 %a)
  %c = call i32 @internal_fn(i32 %b)
  ret i32 %c
}

; Each original function is now a trampoline whose body was moved to an
; internal clone carrying the breaking stub as prologue data.

; CHECK-DAG: define internal i32 @linkonce_fn.{{[0-9]+}}({{.*}} prologue <32 x i8>
; CHECK-DAG: define internal i32 @weak_fn.{{[0-9]+}}({{.*}} prologue <32 x i8>
; CHECK-DAG: define internal i32 @internal_fn.{{[0-9]+}}({{.*}} prologue <32 x i8>

; The trampolines keep the linkage of the functions they replaced.

; CHECK-DAG: define linkonce_odr hidden i32 @linkonce_fn(i32 {{%.*}}) {{.*}}{
; CHECK-DAG: define weak_odr i32 @weak_fn(i32 {{%.*}}) {{.*}}{
; CHECK-DAG: define internal {{.*}}i32 @internal_fn(i32 {{%.*}}) {{.*}}{
