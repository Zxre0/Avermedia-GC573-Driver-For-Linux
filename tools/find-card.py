#!/usr/bin/env python3
"""Find exactly one GC573, or validate an explicitly selected PCI address."""
from pathlib import Path
import re
import sys

IDS = {'vendor':'0x1461', 'device':'0x0054', 'subsystem_vendor':'0x1461', 'subsystem_device':'0x5730'}

def find_card(bdf=None, root=Path('/sys/bus/pci/devices')):
    if bdf and not re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]', bdf):
        raise ValueError('Invalid PCI address')
    matches = []
    for dev in ([root / bdf.lower()] if bdf else sorted(root.iterdir())):
        try:
            if all((dev / key).read_text().strip().lower() == value for key,value in IDS.items()):
                matches.append(dev.name)
        except OSError:
            continue
    if len(matches) != 1:
        raise ValueError(f'Expected one GC573, found {len(matches)}; select its PCI address explicitly.')
    return matches[0]

if __name__ == '__main__':
    if len(sys.argv) > 2:
        sys.exit('Usage: find-card.py [PCI-address]')
    try:
        print(find_card(sys.argv[1] if len(sys.argv) == 2 else None))
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
