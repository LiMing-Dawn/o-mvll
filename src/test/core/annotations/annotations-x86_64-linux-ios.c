//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

// REQUIRES: x86-registered-target && apple_abi

// Drive the Arithmetic pass purely from a source-level function annotation.
// `memcpy_xor` carries `__attribute__((annotate("insreplace")))`.

// RUN:                                                          clang -target x86_64-pc-linux-gnu                         -O1 -S %s -o - | FileCheck --check-prefix=DEFAULT %s
// The annotation matches the requested keyword -> pass is enabled.
// RUN: env OMVLL_CONFIG=%S/config_annotation_match.py           clang -target x86_64-pc-linux-gnu -fpass-plugin=%libOMVLL -O1 -S %s -o - | FileCheck --check-prefix=CHECK %s
// A different annotation keyword is requested -> pass stays disabled.
// RUN: env OMVLL_CONFIG=%S/config_annotation_nomatch.py         clang -target x86_64-pc-linux-gnu -fpass-plugin=%libOMVLL -O1 -S %s -o - | FileCheck --check-prefix=DEFAULT %s

// DEFAULT-LABEL: memcpy_xor:
// DEFAULT:       .LBB0_2:
// DEFAULT:           movzbl	(%rsi,%rcx), %edx
// DEFAULT:           xorb	$35, %dl
// DEFAULT:           movb	%dl, (%rdi,%rcx)
// DEFAULT:           incq	%rcx
// DEFAULT:           cmpq	%rcx, %rax
// DEFAULT:           jne	.LBB0_2

// CHECK-LABEL: memcpy_xor:
// CHECK:       .LBB0_2:
// CHECK-NEXT:     movsbl	(%rsi,%rcx), %edx
// CHECK-NEXT:     movl	%edx, %r8d
// CHECK-NEXT:     notl	%r8d
// CHECK-NEXT:     orl	$-36, %r8d
// CHECK-NEXT:     addl	%edx, %r8d
// CHECK-NEXT:     addl	$36, %r8d
// CHECK-NEXT:     andl	$35, %edx
// CHECK-NEXT:     negl	%edx
// CHECK-NEXT:     movl	%r8d, %r9d
// CHECK-NEXT:     xorl	%edx, %r9d
// CHECK-NEXT:     andl	%r8d, %edx
// CHECK-NEXT:     leal	(%r9,%rdx,2), %edx
// CHECK-NEXT:     movb	%dl, (%rdi,%rcx)
// CHECK-NEXT:     incq	%rcx
// CHECK-NEXT:     cmpq	%rcx, %rax
// CHECK-NEXT:     jne	.LBB0_2

__attribute__((annotate("insreplace")))
void memcpy_xor(char *dst, const char *src, unsigned len) {
  for (unsigned i = 0; i < len; i += 1) {
    dst[i] = src[i] ^ 35;
  }
  dst[len] = '\0';
}
