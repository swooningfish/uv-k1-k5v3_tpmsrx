#!/usr/bin/env bash
# Build TPMSRX.app. Based on the build.sh of the F4HWN overlay apps.
#
# Uses the SDK of the firmware tree when this folder sits in App/apps/,
# otherwise the one fetched into sdk/ by tools/fetch_sdk.py.
set -euo pipefail

cd "$(dirname "$0")"

APP=tpmsrx
APP_VER="0.3"
APP_API_MIN=3
APP_VMA=${APP_VMA:-0x20000280}
PYTHON=${PYTHON:-python3}

if [ -f ../app_api.h ] && [ -f ../pack_app.py ]; then SDK=..; else SDK=sdk; fi
[ -f "$SDK/app_api.h" ] || { echo "❌ SDK not found: run $PYTHON tools/fetch_sdk.py"; exit 1; }

CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
command -v arm-none-eabi-gcc >/dev/null 2>&1 && { CC=arm-none-eabi-gcc; OBJCOPY=arm-none-eabi-objcopy; }

command -v "$CC" >/dev/null 2>&1 || { echo "❌ ARM compiler not found: $CC"; exit 1; }
command -v "$OBJCOPY" >/dev/null 2>&1 || { echo "❌ ARM objcopy not found: $OBJCOPY"; exit 1; }

OUT_DIR=build
mkdir -p "$OUT_DIR"

CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -fno-jump-tables -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -I$SDK -I. -I$OUT_DIR"

rm -f "$OUT_DIR"/*.app "$OUT_DIR"/*.elf "$OUT_DIR"/*.bin "$OUT_DIR"/*.map \
      "$OUT_DIR"/*_assets.h "$OUT_DIR"/*_assets.bin

# set, UV Studio name, compiler flag. UK and EU share a flavour. North America is separate.
SETS=(
  "eu|TPMS EU|TPMS_FLAVOUR_EU"
  "na|TPMS NA|TPMS_FLAVOUR_NA"
  "eu2|TPMS EU2|TPMS_FLAVOUR_EU2"
  "na2|TPMS NA2|TPMS_FLAVOUR_NA2"
)
if [ "${1:-}" != "" ]; then
  want="$1"
  SETS=()
  case "$want" in
    eu)  SETS=("eu|TPMS EU|TPMS_FLAVOUR_EU") ;;
    na)  SETS=("na|TPMS NA|TPMS_FLAVOUR_NA") ;;
    eu2) SETS=("eu2|TPMS EU2|TPMS_FLAVOUR_EU2") ;;
    na2) SETS=("na2|TPMS NA2|TPMS_FLAVOUR_NA2") ;;
    *) echo "unknown flavour: $want"; exit 1 ;;
  esac
fi

CUR=""
trap 'printf "\n  ❌ %s build failed\n" "$CUR"' ERR

for spec in "${SETS[@]}"; do
  IFS='|' read -r set name def <<< "$spec"
  CUR="$name"
  out="${name// /}"
  printf '  🔨 %-12s\n' "$name"
  FLAVOUR="$set" SDK="$SDK" "$PYTHON" ./gen_assets.py \
      "${OUT_DIR}/${APP}_assets.bin" "${OUT_DIR}/${APP}_assets.h"
  "$CC" $CFLAGS -D"$def"=1 \
      -nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} \
      -Wl,--gc-sections -Wl,-Map="${OUT_DIR}/tpms_${set}.map" \
      -Wl,--build-id=none -Wl,--no-warn-rwx-segments \
      "${APP}_app.c" -lgcc -o "${OUT_DIR}/tpms_${set}.elf"
  "$OBJCOPY" -O binary "${OUT_DIR}/tpms_${set}.elf" "${OUT_DIR}/tpms_${set}.bin"
  "$PYTHON" "$SDK/pack_app.py" "${OUT_DIR}/tpms_${set}.bin" "${OUT_DIR}/${out}.app" \
      --name "$name" --ver "$APP_VER" --api-min "$APP_API_MIN" \
      --vma "${APP_VMA}" --shortcut none \
      --assets "${OUT_DIR}/${APP}_assets.bin" >/dev/null
  BYTES=$(wc -c < "${OUT_DIR}/tpms_${set}.bin")
  if [ "$BYTES" -gt 4096 ]; then
      printf '  🚨 %-12s OVERFLOWS 4 KiB (%d B)\n' "$name" "$BYTES"
      exit 1
  fi
  printf '  ✅ %-12s %4d B  (%d%% of 4 KiB)  ->  %s/%s.app\n' \
         "$name" "$BYTES" "$(( BYTES * 100 / 4096 ))" "$OUT_DIR" "$out"
done
trap - ERR
