#!/usr/bin/env bash
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bin="$HOME/.local/bin"
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$bin" "$apps"
# The launcher stays valid when the repository path contains spaces.
printf '#!/usr/bin/env bash\nexec python3 %q "$@"\n' "$project/app/gc573_control.py" > "$bin/gc573-control"
chmod 0755 "$bin/gc573-control"
python3 - "$project/packaging/gc573-control.desktop" "$apps/gc573-control.desktop" "$bin/gc573-control" <<'PY'
from pathlib import Path
import sys
source, target, exe = sys.argv[1:]
Path(target).write_text(Path(source).read_text().replace('Exec=gc573-control', 'Exec="' + exe.replace('\\','\\\\').replace('"','\\"') + '"'))
PY
printf 'Installed GC573 Control in your application menu. Requires Python, PyGObject and GTK4.\n'
