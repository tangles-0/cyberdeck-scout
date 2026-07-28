#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SKETCH_NAME="$(basename "$SCRIPT_DIR")"

FQBN="${FQBN:-megaTinyCore:megaavr:atxy4:chip=404,clock=20internal,wiremode=mors,PWMmux=I6}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/dist}"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SCRIPT_DIR"

ELF="$BUILD_DIR/$SKETCH_NAME.ino.elf"
BIN="$OUT_DIR/$SKETCH_NAME.bin"

if [[ ! -f "$ELF" ]]; then
  echo "Missing compiled ELF: $ELF" >&2
  exit 1
fi

if [[ -n "${AVR_OBJCOPY:-}" ]]; then
  OBJCOPY="$AVR_OBJCOPY"
elif command -v avr-objcopy >/dev/null 2>&1; then
  OBJCOPY="$(command -v avr-objcopy)"
else
  AVR_GCC_PATH="$(
    arduino-cli compile --fqbn "$FQBN" --show-properties "$SCRIPT_DIR" \
      | awk -F= '$1 == "runtime.tools.avr-gcc.path" { print $2; exit }'
  )"
  OBJCOPY="$AVR_GCC_PATH/bin/avr-objcopy"
fi

if [[ ! -x "$OBJCOPY" ]]; then
  echo "avr-objcopy not found. Set AVR_OBJCOPY=/path/to/avr-objcopy." >&2
  exit 1
fi

"$OBJCOPY" -O binary -R .eeprom "$ELF" "$BIN"

printf 'Wrote %s (%s bytes)\n' "$BIN" "$(wc -c < "$BIN")"
