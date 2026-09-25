#!/usr/bin/env bash
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bin="$HOME/.local/bin"
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$bin" "$apps"
# The launchers stay valid when the repository path contains spaces.
for app in control preview; do
printf '#!/usr/bin/env bash\nexec python3 %q "$@"\n' "$project/app/gc573_${app}.py" > "$bin/gc573-$app"
chmod 0755 "$bin/gc573-$app"
python3 - "$project/packaging/gc573-$app.desktop" "$apps/gc573-$app.desktop" "$bin/gc573-$app" "$app" <<'PY'
from pathlib import Path
import sys
source, target, exe, app = sys.argv[1:]
Path(target).write_text(Path(source).read_text().replace('Exec=gc573-' + app, 'Exec="' + exe.replace('\\','\\\\').replace('"','\\"') + '"'))
PY
done
printf 'Installed GC573 Control and GC573 Preview in your application menu.\n'
