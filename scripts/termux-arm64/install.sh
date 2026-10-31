#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

usage() {
  echo "Usage: $0 /absolute/path/to/android-ndk-r29"
  echo "Installs a separate clang-omvll driver; the existing clang-21 is not replaced."
}

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

ndk="$1"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
prebuilt_root="$ndk/toolchains/llvm/prebuilt"
host_dir="$(find "$prebuilt_root" -mindepth 1 -maxdepth 1 -type d -name 'linux-arm64' -print -quit)"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "This bundle only supports an AArch64 Termux host" >&2
  exit 1
fi
if [[ -z "$host_dir" || ! -d "$host_dir/bin" ]]; then
  echo "Could not find $prebuilt_root/linux-arm64" >&2
  exit 1
fi

install_root="$host_dir/lib/omvll"
bin_dir="$host_dir/bin"
mkdir -p "$install_root/lib" "$install_root/python"

install -m 0755 "$script_dir/payload/bin/clang-21-omvll" "$bin_dir/clang-21-omvll"
cp -a "$script_dir/payload/lib/." "$install_root/lib/"
cp -a "$script_dir/payload/python/." "$install_root/python/"

python_stdlib="$(find "$install_root/python" -mindepth 1 -maxdepth 1 -type d -name 'python3*' -print -quit)"
if [[ -z "$python_stdlib" ]]; then
  echo "The packaged Python standard library is missing" >&2
  exit 1
fi

wrapper="$bin_dir/clang-omvll"
cat > "$wrapper" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
set -e
bin_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
install_root="$(cd -- "$bin_dir/../lib/omvll" && pwd)"
python_stdlib="$(find "$install_root/python" -mindepth 1 -maxdepth 1 -type d -name 'python3*' -print -quit)"
export LD_LIBRARY_PATH="$install_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OMVLL_PYTHONPATH="${OMVLL_PYTHONPATH:-$python_stdlib}"
exec "$bin_dir/clang-21-omvll" \
  -fpass-plugin="$install_root/lib/libOMVLL.so" "$@"
EOF
chmod 0755 "$wrapper"

cxx_wrapper="$bin_dir/clang++-omvll"
cat > "$cxx_wrapper" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
set -e
bin_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$bin_dir/clang-omvll" --driver-mode=g++ "$@"
EOF
chmod 0755 "$cxx_wrapper"

echo "Installed without replacing the NDK's existing Clang:"
echo "  $wrapper"
echo "  $cxx_wrapper"
echo
echo "Quick test:"
echo "  export OMVLL_CONFIG=$script_dir/examples/omvll_config.py"
echo "  $wrapper -O2 -c probe.c -o probe.o"
