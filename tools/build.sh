#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel_release=${1:-$(uname -r)}
kernel_dir="/lib/modules/$kernel_release/build"
if [[ ! -f "$kernel_dir/Makefile" ]]; then
    kernel_dir="$project_dir/.build/usr/lib/modules/$kernel_release/build"
fi
if [[ ! -f "$kernel_dir/Makefile" ]]; then
    echo "Missing headers for $kernel_release. Install matching headers or boot an installed kernel." >&2
    exit 1
fi
actual_release=$(cat "$kernel_dir/include/config/kernel.release")
if [[ "$actual_release" != "$kernel_release" ]]; then
    echo "Header release mismatch: $actual_release != $kernel_release" >&2
    exit 1
fi
make_args=(W=1)
if grep -q '^CONFIG_CC_IS_CLANG=y' "$kernel_dir/.config"; then
    make_args+=(LLVM=1)
else
    make_args+=("CC=${CC:-gcc}")
fi
make -C "$kernel_dir" M="$project_dir/driver" "${make_args[@]}" modules
mkdir -p "$project_dir/.build/modules/$kernel_release"
cp "$project_dir/driver/gc573_native.ko" "$project_dir/.build/modules/$kernel_release/"
modinfo "$project_dir/.build/modules/$kernel_release/gc573_native.ko"
