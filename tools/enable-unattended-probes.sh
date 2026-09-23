#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/bin:/usr/sbin
helper=/usr/local/sbin/gc573-codex-probe
policy=/etc/sudoers.d/gc573-codex
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $# -gt 1 || ( $# == 1 && $1 != --remove ) ]]; then
    echo "Usage: sudo $0 [--remove]" >&2
    exit 2
fi
if [[ $EUID -ne 0 ]]; then
    echo "One-time activation requires: sudo $project/tools/enable-unattended-probes.sh" >&2
    exit 1
fi
if [[ ${1:-} == --remove ]]; then
    rm -f -- "$policy" "$helper"
    echo 'Password-free GC573 probe access removed. The loaded module is unchanged.'
    exit 0
fi
account=${SUDO_USER:-}
[[ "$account" =~ ^[a-zA-Z_][a-zA-Z0-9_-]*$ && "$account" != root ]] || {
    echo 'Run from your normal account through sudo.' >&2; exit 1;
}
[[ $(id -u "$account") -gt 0 ]]
bdf=$(python3 "$project/tools/find-card.py")
[[ -f "$project/tools/gc573-codex-probe" && -x "$project/tools/load-probe.sh" ]]
visudo -cf /etc/sudoers
staging=$(mktemp -d /tmp/gc573-access.XXXXXXXX)
policy_staging=/etc/sudoers.d/.gc573-codex.$$
trap 'rm -rf -- "$staging"; rm -f -- "$policy_staging"' EXIT
printf '%s ALL=(root) NOPASSWD: /usr/local/sbin/gc573-codex-probe\n' "$account" > "$staging/policy"
visudo -cf "$staging/policy"
python3 - "$project" "$bdf" "$staging/helper" <<'PYCODE'
from pathlib import Path
import shlex, sys
project, bdf, target = sys.argv[1:]
source = (Path(project) / 'tools/gc573-codex-probe').read_text()
Path(target).write_text(source.replace('@PROJECT@', shlex.quote(project)).replace('@BDF@', shlex.quote(bdf)))
PYCODE
bash -n "$staging/helper"
install -o root -g root -m 0755 "$staging/helper" "$helper"
install -o root -g root -m 0440 "$staging/policy" "$policy_staging"
mv -f -- "$policy_staging" "$policy"
if ! visudo -cf /etc/sudoers; then
    rm -f -- "$policy"
    echo 'Activation failed; the new sudo rule was removed.' >&2
    exit 1
fi
"$helper" --check
echo 'Codex can now use tools/run-probe.sh MODE and read the saved output.'
echo 'This grants root execution to the project loader and its kernel module.'
