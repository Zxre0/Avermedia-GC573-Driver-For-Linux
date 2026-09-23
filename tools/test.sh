#!/usr/bin/env bash
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project"
mkdir -p .build/tests
sources=()
for source in driver/*.c; do
    case "$source" in
        driver/gc573_pci.c|driver/gc573_capture.c|driver/gc573_audio.c|*.mod.c) continue ;;
    esac
    sources+=("$source")
done
for test in tests/*_test.c; do
    output=".build/tests/$(basename "$test" .c)"
    "${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -Idriver "$test" "${sources[@]}" -o "$output"
    "$output"
done
python3 -m unittest discover -s tests -p '*_test.py'
for script in tools/*.sh tools/gc573-codex-probe; do bash -n "$script"; done
