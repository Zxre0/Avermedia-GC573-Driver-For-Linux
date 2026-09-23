#!/usr/bin/env python3
"""Collect PCI metadata only: no MMIO access, configuration writes or sudo."""
import argparse
import datetime
import json
from pathlib import Path
import platform
import re
import struct


def read_text(path):
    try:
        return path.read_text().strip()
    except OSError as exc:
        return {"error": str(exc)}


def decode_header(data):
    if len(data) < 64:
        raise ValueError(f"PCI header is only {len(data)} bytes (need 64)")
    vendor, device, command, status = struct.unpack_from("<HHHH", data)
    subvendor, subdevice = struct.unpack_from("<HH", data, 0x2C)
    return {
        "vendor": f"0x{vendor:04x}", "device": f"0x{device:04x}",
        "subsystem_vendor": f"0x{subvendor:04x}",
        "subsystem_device": f"0x{subdevice:04x}",
        "command": f"0x{command:04x}", "status": f"0x{status:04x}",
        "memory_decode_enabled": bool(command & 2),
        "bus_master_enabled": bool(command & 4),
        "revision": data[8], "header_type": data[14],
        "bars_raw": [f"0x{x:08x}" for x in struct.unpack_from("<6I", data, 0x10)],
    }


def collect(bdf):
    device = Path("/sys/bus/pci/devices") / bdf
    if not device.is_dir():
        raise ValueError(f"PCI device {bdf} does not exist")
    fields = ("vendor", "device", "subsystem_vendor", "subsystem_device", "class",
              "revision", "resource", "current_link_speed", "current_link_width",
              "max_link_speed", "max_link_width", "enable", "irq", "modalias")
    result = {
        "collected_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "kernel": platform.release(), "bdf": bdf,
        "pci": {name: read_text(device / name) for name in fields},
        "driver": (device / "driver").resolve().name if (device / "driver").is_symlink() else None,
        "iommu_group": (device / "iommu_group").resolve().name if (device / "iommu_group").is_symlink() else None,
        "installed_kernel_trees": sorted(p.name for p in Path("/lib/modules").iterdir() if p.is_dir()),
        "video_devices": sorted(str(p) for p in Path("/dev").glob("video*")),
    }
    try:
        with (device / "config").open("rb") as stream:
            header = stream.read(64)
        result["pci_header"] = decode_header(header)
        result["pci_header_hex"] = header.hex()
    except (OSError, ValueError) as exc:
        result["pci_header"] = {"error": str(exc)}
    if (device / "bringup_status").exists():
        result["native_probe"] = read_text(device / "bringup_status")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bdf")
    args = parser.parse_args()
    if args.bdf is None:
        import subprocess
        detected = subprocess.run([str(Path(__file__).with_name('find-card.py'))], text=True, capture_output=True)
        if detected.returncode:
            parser.error(detected.stderr.strip())
        args.bdf = detected.stdout.strip()
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", args.bdf):
        parser.error("Expected a PCI address such as 0000:05:00.0")
    try:
        result = collect(args.bdf.lower())
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{exc}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
