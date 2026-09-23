#!/usr/bin/env bash
# Noninteractive entry point for Codex; tee runs as the normal user.
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
helper=/usr/local/sbin/gc573-codex-probe
if [[ $# != 1 || ! $1 =~ ^--[a-z][a-z0-9-]*$ ]]; then
    echo "Usage: $0 --MODE (for example --splitter-tx-ports), or --check" >&2
    exit 2
fi
if [[ ! -x "$helper" ]]; then
    echo "Access has not been activated. Run once: sudo $project_dir/tools/enable-unattended-probes.sh" >&2
    exit 1
fi
if [[ $1 == --check ]]; then
    exec sudo -n "$helper" --check
fi
sudo -n "$helper" --check >/dev/null
mkdir -p -- "$project_dir/reports"
log=$(mktemp "$project_dir/reports/auto-$(date -u +%Y%m%dT%H%M%SZ)-${1#--}-XXXXXX.log")
printf 'Probe output: %s\n' "$log"
# Preserve failure status even though output is also saved.
sudo -n "$helper" "$1" 2>&1 | tee "$log"
