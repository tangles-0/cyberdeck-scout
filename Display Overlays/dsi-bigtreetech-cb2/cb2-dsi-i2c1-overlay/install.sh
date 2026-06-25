#!/usr/bin/env bash
set -euo pipefail

overlay_name="cb2-dsi-i2c1-rpi7"
src_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dtbo_src="${src_dir}/${overlay_name}.dtbo"
env_file="/boot/armbianEnv.txt"
backup_file="/boot/armbianEnv.txt.pre-${overlay_name}"

if [[ $EUID -ne 0 ]]; then
	echo "Run with sudo: sudo ./install.sh" >&2
	exit 1
fi

if [[ ! -f "${dtbo_src}" ]]; then
	echo "Missing ${dtbo_src}" >&2
	echo "Build it with: dtc -@ -I dts -O dtb -o ${overlay_name}.dtbo ${overlay_name}.dts" >&2
	exit 1
fi

if [[ ! -f "${env_file}" ]]; then
	echo "Missing ${env_file}" >&2
	exit 1
fi

mkdir -p /boot/overlay-user
install -m 0644 "${dtbo_src}" "/boot/overlay-user/${overlay_name}.dtbo"

if [[ ! -f "${backup_file}" ]]; then
	cp "${env_file}" "${backup_file}"
	echo "Backed up ${env_file} to ${backup_file}"
else
	echo "Backup already exists: ${backup_file}"
fi

python3 - "${env_file}" "${overlay_name}" <<'PY'
from pathlib import Path
import sys

env_path = Path(sys.argv[1])
overlay_name = sys.argv[2]
lines = env_path.read_text().splitlines()

out = []
seen_overlays = False
seen_user_overlays = False

for line in lines:
    if line.startswith("overlays="):
        out.append("overlays=")
        seen_overlays = True
    elif line.startswith("user_overlays="):
        existing = line.split("=", 1)[1].split()
        if overlay_name not in existing:
            existing.append(overlay_name)
        out.append("user_overlays=" + " ".join(existing))
        seen_user_overlays = True
    else:
        out.append(line)

if not seen_overlays:
    out.append("overlays=")

if not seen_user_overlays:
    inserted = False
    new = []
    for line in out:
        new.append(line)
        if line.startswith("overlays=") and not inserted:
            new.append(f"user_overlays={overlay_name}")
            inserted = True
    out = new

env_path.write_text("\n".join(out) + "\n")
PY

sync
echo "Installed /boot/overlay-user/${overlay_name}.dtbo"
echo "Updated ${env_file}"
echo "Reboot with: sudo reboot"
