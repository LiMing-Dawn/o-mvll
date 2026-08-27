;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; The trampoline emitted for a swiftcc function performs a musttail call, which
; AArch64 cannot honour when the function takes a swifterror argument: it is
; passed in x21, a callee-saved register, and SelectionDAG tracks it in its own
; virtual register rather than the register's live-in value. Lowering aborts
; with "failed to perform tail call elimination on a call site marked musttail",
; so the pass must leave those functions alone. Every Swift `throws` function
; has a swifterror argument.

; RUN: env OMVLL_CONFIG=%S/config_all.py clang -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O1 -S -emit-llvm %s -o - \
; RUN:   | FileCheck --implicit-check-not=@throwing_fn. %s

; Lowering the module all the way down to assembly must not abort.

; RUN: env OMVLL_CONFIG=%S/config_all.py clang -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O1 -S %s -o /dev/null

; The swifterror function keeps its body: no clone (checked module-wide by the
; --implicit-check-not above), no prologue data on its definition line, and no
; musttail call in it.

; CHECK-LABEL: define swiftcc i64 @throwing_fn({{.*}}) local_unnamed_addr #{{[0-9]+}} {
; CHECK:         store ptr inttoptr (i64 1 to ptr), ptr %err
; CHECK-NOT:     musttail

; A swiftcc function without swifterror is broken as usual: the body moves to a
; clone carrying the breaking stub as prologue data, and the original becomes a
; musttail trampoline branching past that stub. Both end up after the untouched
; functions in the module.

; CHECK-LABEL: define internal swiftcc i64 @plain_fn.{{[0-9]+}}(
; CHECK-SAME:    prologue <32 x i8>
; CHECK-LABEL: define swiftcc i64 @plain_fn(
; CHECK:         musttail call swiftcc i64

define swiftcc i64 @plain_fn(i64 %x, ptr swiftself %self) noinline {
entry:
  %cmp = icmp slt i64 %x, 0
  br i1 %cmp, label %fail, label %ok

fail:
  ret i64 0

ok:
  %mul = mul i64 %x, 3
  ret i64 %mul
}

define swiftcc i64 @throwing_fn(i64 %x, ptr swiftself %self, ptr swifterror %err) noinline {
entry:
  %cmp = icmp slt i64 %x, 0
  br i1 %cmp, label %fail, label %ok

fail:
  store ptr inttoptr (i64 1 to ptr), ptr %err, align 8
  ret i64 0

ok:
  %mul = mul i64 %x, 3
  ret i64 %mul
}
