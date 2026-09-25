#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Remove project-owned user artifacts; never delete the checkout or recordings."""
import argparse
import os
from pathlib import Path
import shutil

PROJECT = Path(__file__).resolve().parents[1]


def remove(path):
    # A symlink is removed itself, never followed into another directory.
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def cleanup(project, home, environ, keep_settings=False):
    config = Path(environ.get('XDG_CONFIG_HOME') or home / '.config')
    data = Path(environ.get('XDG_DATA_HOME') or home / '.local/share')
    for app in ('control', 'preview'):
        remove(home / f'.local/bin/gc573-{app}')
        remove(data / f'applications/gc573-{app}.desktop')
    if not keep_settings:
        remove(config / 'gc573-control')
    if environ.get('XDG_RUNTIME_DIR'):
        remove(Path(environ['XDG_RUNTIME_DIR']) / 'gc573-native-start.lock')
    # Only outputs of the public build/test scripts, not .build backups,
    # private research, source archives or recordings in reports/.
    for name in ('modules', 'tests'):
        remove(project / '.build' / name)
    patterns = ('*.o', '*.ko', '*.mod', '*.mod.c', '.*.cmd', '*.o.d',
                'Module.symvers', 'modules.order', '.tmp_versions')
    for pattern in patterns:
        for path in (project / 'driver').glob(pattern):
            remove(path)
    for directory in ('app', 'tools', 'tests'):
        remove(project / directory / '__pycache__')
    for path in (project / 'reports').glob('auto-*.log'):
        remove(path)
    remove(project / 'reports/automatic-build.log')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep-settings', action='store_true')
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('Run ./uninstall.sh as the normal desktop user.')
    cleanup(PROJECT, Path.home(), os.environ, args.keep_settings)


if __name__ == '__main__':
    main()
