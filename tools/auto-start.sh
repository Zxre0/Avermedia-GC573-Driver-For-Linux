#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec 9>"${XDG_RUNTIME_DIR:?}/gc573-native-start.lock"
flock -n 9 || exit 75
mkdir -p "$project_dir/reports"
# Build without root, including after a kernel update when matching headers exist.
"$project_dir/tools/build.sh" > "$project_dir/reports/automatic-build.log" 2>&1
"$project_dir/tools/run-probe.sh" --start
python3 "$project_dir/app/gc573_control.py" --restore
