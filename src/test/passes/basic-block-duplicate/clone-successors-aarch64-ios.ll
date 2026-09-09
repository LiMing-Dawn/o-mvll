;
; This file is distributed under the Apache License v2.0. See LICENSE for details.
;

; REQUIRES: aarch64-registered-target && apple_abi

; RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
; RUN:         -target arm64-apple-ios17.5.0 -O0 -S -emit-llvm %s -o %t.ll
; RUN: FileCheck --check-prefix=CHAIN %s < %t.ll
; RUN: FileCheck --check-prefix=MERGE %s < %t.ll

; When the successor of a duplicated block has been duplicated as well, the
; cloned block must branch to the cloned successor instead of falling back into
; the original chain.

define void @chain(ptr %p) {
; CHAIN-LABEL: define void @chain(
; CHAIN-SAME: ptr [[P:%.*]])
; CHAIN:       {{^}}a.split.clone:
; CHAIN-NEXT:    store i32 1, ptr [[P]]
; CHAIN-NEXT:    br label %b.split.clone
; CHAIN:       {{^}}b.split.clone:
; CHAIN-NEXT:    store i32 2, ptr [[P]]
; CHAIN-NEXT:    br label %c.split.clone
; CHAIN:       {{^}}c.split.clone:
; CHAIN-NEXT:    store i32 3, ptr [[P]]
; CHAIN-NEXT:    ret void
;
entry:
  br label %a

a:
  store i32 1, ptr %p
  br label %b

b:
  store i32 2, ptr %p
  br label %c

c:
  store i32 3, ptr %p
  ret void
}

; A head block holding PHI nodes cannot be bypassed: rerouting the edge onto
; the cloned tail would skip the PHI nodes altogether. The cloned blocks must
; keep branching to the original successor here.

define i32 @merge(i32 %x) {
; MERGE-LABEL: define i32 @merge(
; MERGE-SAME: i32 [[X:%.*]])
; MERGE:       {{^}}m:
; MERGE-NEXT:    [[PN:%.*]] = phi i32 [ [[A:%.*]], %t.split ], [ [[B:%.*]], %f.split ], [ [[A_CLONE:%.*]], %t.split.clone ], [ [[B_CLONE:%.*]], %f.split.clone ]
; MERGE:       {{^}}t.split.clone:
; MERGE-NEXT:    [[A_CLONE]] = add i32 [[X]], 1
; MERGE-NEXT:    br label %m
; MERGE:       {{^}}f.split.clone:
; MERGE-NEXT:    [[B_CLONE]] = mul i32 [[X]], 3
; MERGE-NEXT:    br label %m
;
entry:
  %c = icmp sgt i32 %x, 0
  br i1 %c, label %t, label %f

t:
  %a = add i32 %x, 1
  br label %m

f:
  %b = mul i32 %x, 3
  br label %m

m:
  %p = phi i32 [ %a, %t ], [ %b, %f ]
  ret i32 %p
}
