#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
work_root="${OMVLL_WORK_ROOT:-${RUNNER_TEMP:-/tmp}/omvll-termux-arm64}"
ndk_revision="${NDK_REVISION:-r29}"
android_api="${ANDROID_API:-25}"
llvm_custom_commit="${LLVM_CUSTOM_COMMIT:-5ba9b351d129c9847a3fd79a48ae08e5986001f3}"
python_version="${PYTHON_VERSION:-3.13.14}"
pybind11_version="${PYBIND11_VERSION:-2.13.6}"
spdlog_version="${SPDLOG_VERSION:-1.10.0}"
target="aarch64-linux-android"

if [[ ! "$ndk_revision" =~ ^r([0-9]+)([a-z]?)$ ]]; then
  echo "NDK_REVISION must look like r29, got: $ndk_revision" >&2
  exit 2
fi
ndk_version="${BASH_REMATCH[1]}"
ndk_letter="${BASH_REMATCH[2]}"

llvm_custom_src="$work_root/llvm-custom"
llvm_root="$work_root/llvm-work"
ndk_dir="$llvm_root/android-ndk-$ndk_revision"
llvm_out="$llvm_root/llvm-$target"
python_src="$work_root/cpython-$python_version"
python_host="aarch64-linux-android"
python_prefix="$python_src/cross-build/$python_host/prefix"
spdlog_src="$work_root/spdlog-$spdlog_version"
spdlog_build="$work_root/spdlog-build"
spdlog_prefix="$work_root/spdlog-prefix"
pybind11_src="$work_root/pybind11-$pybind11_version"
pybind11_build="$work_root/pybind11-build"
pybind11_prefix="$work_root/pybind11-prefix"
omvll_build="$work_root/omvll-build"
stage="$repo_root/dist/termux-arm64"
archive="$repo_root/dist/omvll-termux-arm64-$ndk_revision-api$android_api.tar.xz"

log() {
  printf '\033[1;34m==>\033[0m %s\n' "$*"
}

download() {
  local url="$1"
  local output="$2"
  curl --fail --location --retry 5 --retry-delay 2 --output "$output" "$url"
}

log "Preparing clean build directories"
rm -rf "$work_root" "$stage"
mkdir -p "$work_root" "$llvm_root" "$stage" "$(dirname "$archive")"

log "Fetching llvm-custom at $llvm_custom_commit"
git clone --filter=blob:none https://github.com/HomuHomu833/llvm-custom.git "$llvm_custom_src"
git -C "$llvm_custom_src" checkout --detach "$llvm_custom_commit"

log "Resolving the LLVM revision and Android patches from NDK $ndk_revision"
ROOTDIR="$llvm_root" \
NDK_VERSION="$ndk_version" \
NDK_REVISION="$ndk_letter" \
PLATFORM=bionic \
TARGET="$target" \
bash "$llvm_custom_src/scripts/fetch-source.sh"

log "Converting the bionic compiler build from fully static to plugin-capable"
cp "$llvm_custom_src/scripts/build.sh" "$work_root/build-dynamic-llvm.sh"
sed -i \
  's/CROSS_CFLAGS="-static -fno-sanitize=undefined"; CROSS_LDFLAGS="-static"; LLVM_STATIC=ON/CROSS_CFLAGS="-fPIC -fno-sanitize=undefined"; CROSS_LDFLAGS=""; LLVM_STATIC=OFF/' \
  "$work_root/build-dynamic-llvm.sh"

ROOTDIR="$llvm_root" \
PLATFORM=bionic \
TARGET="$target" \
ANDROID_API="$android_api" \
PROJECTS="clang;lld" \
EXTRA_CMAKE_FLAGS="-DLLVM_ENABLE_PIC=ON -DLLVM_EXPORT_SYMBOLS_FOR_PLUGINS=ON -DLLVM_ENABLE_ZSTD=OFF" \
bash "$work_root/build-dynamic-llvm.sh"

dynamic_clang="$(find "$llvm_out/bin" -maxdepth 1 -type f -name 'clang-[0-9]*' -print -quit)"
if [[ -z "$dynamic_clang" ]]; then
  echo "The dynamic Clang executable was not installed" >&2
  exit 1
fi

ndk_toolchain="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64"
target_runtime_lib="$ndk_toolchain/sysroot/usr/lib/aarch64-linux-android"
cross_cc="$ndk_toolchain/bin/${target}${android_api}-clang"
cross_cxx="$ndk_toolchain/bin/${target}${android_api}-clang++"
cross_strip="$ndk_toolchain/bin/llvm-strip"

log "Building CPython $python_version for Android ARM64"
download \
  "https://www.python.org/ftp/python/$python_version/Python-$python_version.tar.xz" \
  "$work_root/Python-$python_version.tar.xz"
tar -xJf "$work_root/Python-$python_version.tar.xz" -C "$work_root"
mv "$work_root/Python-$python_version" "$python_src"
export ANDROID_HOME="${ANDROID_HOME:-/usr/local/lib/android/sdk}"
python_android_driver=""
for candidate in \
  "$python_src/Android/android.py" \
  "$python_src/Platforms/Android"; do
  if [[ -f "$candidate" ]]; then
    python_android_driver="$candidate"
    break
  fi
done
if [[ -z "$python_android_driver" ]]; then
  echo "Could not locate CPython's Android build driver" >&2
  exit 1
fi
python3 "$python_android_driver" build build
python3 "$python_android_driver" build "$python_host"

python_library="$(find "$python_prefix/lib" -maxdepth 1 \( -type f -o -type l \) -name 'libpython3.*.so' -print -quit)"
python_include="$(find "$python_prefix/include" -maxdepth 1 -type d -name 'python3*' -print -quit)"
python_stdlib="$(find "$python_prefix/lib" -maxdepth 1 -type d -name 'python3*' -print -quit)"
if [[ -z "$python_library" || -z "$python_include" || -z "$python_stdlib" ]]; then
  echo "Could not locate the Android CPython library, headers, or standard library" >&2
  exit 1
fi

log "Fetching pybind11 $pybind11_version"
download \
  "https://github.com/pybind/pybind11/archive/refs/tags/v$pybind11_version.tar.gz" \
  "$work_root/pybind11.tar.gz"
tar -xzf "$work_root/pybind11.tar.gz" -C "$work_root"
cmake -S "$pybind11_src" -B "$pybind11_build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$pybind11_prefix" \
  -DPYBIND11_INSTALL=ON \
  -DPYBIND11_TEST=OFF
cmake --build "$pybind11_build" --target install

log "Building spdlog $spdlog_version for Android ARM64"
download \
  "https://github.com/gabime/spdlog/archive/refs/tags/v$spdlog_version.tar.gz" \
  "$work_root/spdlog.tar.gz"
tar -xzf "$work_root/spdlog.tar.gz" -C "$work_root"
cmake -S "$spdlog_src" -B "$spdlog_build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk_dir/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM="android-$android_api" \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$spdlog_prefix" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DSPDLOG_BUILD_SHARED=OFF \
  -DSPDLOG_BUILD_EXAMPLE=OFF \
  -DSPDLOG_BUILD_TESTS=OFF
cmake --build "$spdlog_build" --target install --parallel "$(nproc)"

log "Building O-MVLL against the matching PIC LLVM libraries"
cmake -S "$repo_root/src" -B "$omvll_build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk_dir/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM="android-$android_api" \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$cross_cc" \
  -DCMAKE_CXX_COMPILER="$cross_cxx" \
  -DCMAKE_STRIP="$cross_strip" \
  -DLLVM_DIR="$llvm_out/lib/cmake/llvm" \
  -Dpybind11_DIR="$pybind11_prefix/share/cmake/pybind11" \
  -DPYBIND11_NOPYTHON=ON \
  -Dspdlog_DIR="$spdlog_prefix/lib/cmake/spdlog" \
  -DOMVLL_ABI=CustomAndroid \
  -DOMVLL_CROSS_PYTHON_INCLUDE_DIR="$python_include" \
  -DOMVLL_CROSS_PYTHON_LIBRARY="$python_library"
cmake --build "$omvll_build" --target OMVLL --parallel "$(nproc)"

omvll_library="$(find "$omvll_build" -type f -name 'libOMVLL.so' -print -quit)"
if [[ -z "$omvll_library" ]]; then
  echo "libOMVLL.so was not produced" >&2
  exit 1
fi

log "Staging the Termux installation bundle"
mkdir -p \
  "$stage/payload/bin" \
  "$stage/payload/lib" \
  "$stage/payload/python" \
  "$stage/examples"
cp "$dynamic_clang" "$stage/payload/bin/clang-21-omvll"
cp "$omvll_library" "$stage/payload/lib/libOMVLL.so"
cp -a "$python_prefix/lib"/libpython3*.so* "$stage/payload/lib/"
cp -a "$python_stdlib" "$stage/payload/python/"
cp "$repo_root/dist/sample-omvll-config.py" "$stage/examples/omvll_config.py"
cp "$repo_root/scripts/termux-arm64/install.sh" "$stage/install.sh"
cp "$repo_root/scripts/termux-arm64/README.md" "$stage/README.md"
chmod +x "$stage/install.sh" "$stage/payload/bin/clang-21-omvll"

while IFS= read -r needed; do
  candidate="$(find "$target_runtime_lib" -type f -name "$needed" -print -quit)"
  if [[ -z "$candidate" ]]; then
    echo "Could not find the AArch64 runtime dependency: $needed" >&2
    exit 1
  fi
  cp -L "$candidate" "$stage/payload/lib/$needed"
done < <(
  "$ndk_toolchain/bin/llvm-readelf" -d "$stage/payload/bin/clang-21-omvll" |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' |
    grep -E '^(libc\+\+_shared|libunwind)\.so$' || true
)

log "Verifying Android ELF metadata and plugin entry point"
staged_elfs=(
  "$stage/payload/bin/clang-21-omvll"
  "$stage/payload/lib/"*.so*
)
for elf in "${staged_elfs[@]}"; do
  if ! "$ndk_toolchain/bin/llvm-readelf" -h "$elf" |
       grep -q 'Machine:.*AArch64'; then
    echo "Staged runtime is not AArch64: $elf" >&2
    "$ndk_toolchain/bin/llvm-readelf" -h "$elf" >&2
    exit 1
  fi
done

verify="$stage/VERIFY.txt"
{
  echo "NDK=$ndk_revision"
  echo "ANDROID_API=$android_api"
  echo "LLVM_CUSTOM_COMMIT=$llvm_custom_commit"
  echo
  file "$stage/payload/bin/clang-21-omvll"
  file "$stage/payload/lib/libOMVLL.so"
  echo
  echo "ELF machines:"
  for elf in "${staged_elfs[@]}"; do
    printf '%s: ' "$(basename "$elf")"
    "$ndk_toolchain/bin/llvm-readelf" -h "$elf" |
      sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p'
  done
  echo
  echo "clang NEEDED:"
  "$ndk_toolchain/bin/llvm-readelf" -d "$stage/payload/bin/clang-21-omvll" |
    grep 'Shared library'
  echo
  echo "O-MVLL NEEDED:"
  "$ndk_toolchain/bin/llvm-readelf" -d "$stage/payload/lib/libOMVLL.so" |
    grep 'Shared library'
  echo
  echo "pass-plugin entry point:"
  "$ndk_toolchain/bin/llvm-nm" -D "$stage/payload/lib/libOMVLL.so" |
    grep 'llvmGetPassPluginInfo'
} | tee "$verify"

if grep -q 'statically linked' "$verify"; then
  echo "The staged Clang is still fully static and cannot load pass plugins" >&2
  exit 1
fi
if ! grep -q 'llvmGetPassPluginInfo' "$verify"; then
  echo "The O-MVLL pass-plugin entry point is missing" >&2
  exit 1
fi

tar -C "$(dirname "$stage")" -cJf "$archive" "$(basename "$stage")"
sha256sum "$archive" | tee "$archive.sha256"
log "Bundle ready: $archive"
