;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; Verify two properties:
;   1. The same seed produces identical output on repeated runs (determinism).
;   2. Two different seeds produce different output (shuffling is actually live).
;
; The function body contains enough fully-independent instructions that any two
; distinct seeds will almost certainly produce different orderings.

; RUN: rm -rf %t && mkdir -p %t

; Run with seed 100 twice — outputs must be identical.
; RUN: env OMVLL_CONFIG=%S/config_seed_100.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o %t/seed100_a.ll
; RUN: env OMVLL_CONFIG=%S/config_seed_100.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o %t/seed100_b.ll
; RUN: diff %t/seed100_a.ll %t/seed100_b.ll

; Run with seed 200 — output must differ from seed 100.
; RUN: env OMVLL_CONFIG=%S/config_seed_200.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o %t/seed200.ll
; RUN: not diff %t/seed100_a.ll %t/seed200.ll

define i32 @multi_independent(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %p = add i32 %a, 1
  %q = add i32 %b, 2
  %r = add i32 %c, 3
  %s = add i32 %d, 4
  %t = add i32 %a, 5
  %u = add i32 %b, 6
  %v = add i32 %c, 7
  %w = add i32 %d, 8
  %sum = add i32 %p, %q
  ret i32 %sum
}
