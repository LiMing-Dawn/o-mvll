//
// This file is distributed under the Apache License v2.0. See LICENSE for details.
//

// REQUIRES: aarch64-registered-target && android_abi

// A C++ inline function is emitted in every TU that uses it, as linkonce_odr in
// an ELF COMDAT group. IndirectBranch turns the branch below into a jump table
// of blockaddress() constants, which must join that same group: otherwise it
// outlives the group the linker discards and is left relocating against labels
// in a discarded section.

// Build the same source twice, so two TUs define pick().
// RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
// RUN:         -target aarch64-linux-android -O0 -fPIC -c -DENTRY=a %s -o %t.a.o
// RUN: env OMVLL_CONFIG=%S/config_all.py clang++ -fpass-plugin=%libOMVLL \
// RUN:         -target aarch64-linux-android -O0 -fPIC -c -DENTRY=b %s -o %t.b.o

// The linker keeps one copy of pick() and discards the other. This fails unless
// the discarded copy's jump table was discarded along with it. -nostdlib keeps
// the link down to just these two objects.
// RUN: clang++ -target aarch64-linux-android -shared -nostdlib %t.a.o %t.b.o -o %t.so

inline int pick(int x) {
  if (x > 0)
    return x + 1;
  return -x;
}

int ENTRY(int x) { return pick(x); }
