//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

// REQUIRES: apple_abi && aarch64-registered-target

// Swift async functions are coroutines when this pass runs: LLVM splits them
// into their continuation funclets ($s...TQ0_, $s...TY1_) later in the
// pipeline. A funclet carved out of our clone inherits the clone's prologue
// data, and the async runtime resumes it at its own symbol, landing in the
// middle of the 32-byte breaking stub instead of on real code, which faults at
// runtime. The pass must leave coroutines alone, so no swifttailcc function
// may end up carrying prologue data.

// RUN: rm -rf %t && mkdir -p %t
// RUN: env OMVLL_CONFIG=%S/config_all.py swift-frontend -frontend -c \
// RUN:     -primary-file %s -target arm64-apple-ios17.5.0 -module-name breakcfg \
// RUN:     -Onone -module-cache-path %t -load-pass-plugin=%libOMVLL -emit-ir \
// RUN:     %EXTRA_SWIFT_FLAGS -o - \
// RUN:   | FileCheck --implicit-check-not='define {{.*}}swifttailcc {{.*}}prologue' %s

// The async machinery has to actually be there for the check above to mean
// something, and the pass has to still be transforming the ordinary functions.

// CHECK-DAG: define {{.*}}swifttailcc void @"$s8breakcfg4workyS2iYaF"
// CHECK-DAG: define {{.*}}swifttailcc void @"$s8breakcfg4workyS2iYaFTQ0_"
// CHECK-DAG: define internal swiftcc i64 @"$s8breakcfg4syncyS2iF.{{[0-9]+}}"{{.*}} prologue <32 x i8>

@inline(never)
func work(_ x: Int) async -> Int {
  await Task.yield()
  return x &* 3
}

@inline(never)
public func run(_ x: Int) async -> Int {
  var acc = 0
  for i in 0 ..< 4 {
    acc &+= await work(x &+ i)
  }
  return acc
}

@inline(never)
public func sync(_ x: Int) -> Int {
  return x &* 7 &+ 1
}
