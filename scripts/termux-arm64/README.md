# O-MVLL for native Termux ARM64

This bundle contains a bionic AArch64 Clang that is dynamically linked, an
O-MVLL pass plugin built against the exact same LLVM 21 revision, and an
Android CPython runtime. It is intended for an NDK `linux-arm64` prebuilt
running directly in Termux.

The stock `clang-21` is not overwritten. The installer adds two separate
drivers, `clang-omvll` and `clang++-omvll`.

## Install

```bash
pkg install bash coreutils findutils
tar -xJf omvll-termux-arm64-r29-api25.tar.xz
cd termux-arm64
./install.sh "$HOME/android-ndk-r29"
```

## Test

```bash
export NDK="$HOME/android-ndk-r29"
export OMVLL_CONFIG="$PWD/examples/omvll_config.py"
printf 'int test_value(void){return 1;}\n' > "$PREFIX/tmp/omvll_probe.c"
"$NDK/toolchains/llvm/prebuilt/linux-arm64/bin/clang-omvll" \
  -O2 -c "$PREFIX/tmp/omvll_probe.c" \
  -o "$PREFIX/tmp/omvll_probe.o"
```

The wrapper sets `LD_LIBRARY_PATH`, `OMVLL_PYTHONPATH`, and
`-fpass-plugin=.../libOMVLL.so`. You may override `OMVLL_PYTHONPATH` or
`OMVLL_CONFIG` before invoking it.

## Why another Clang is included

An AArch64 plugin by itself is insufficient when the host Clang is fully
static. Such a Clang contains Android's `libdl.a` stub and every
`-fpass-plugin` attempt fails before LLVM reads the plugin entry point. This
bundle keeps LLVM libraries static/PIC but makes the Clang process dynamically
linked to bionic, so Android's real dynamic loader can load `libOMVLL.so`.
