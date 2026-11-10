#!/usr/bin/env bash
# Build TPMSRX.app. Based on the build.sh of the F4HWN overlay apps.
#
# Uses the SDK of the firmware tree when this folder sits in App/apps/,
# otherwise the one fetched into sdk/ by tools/fetch_sdk.py.
set -euo pipefail

cd "$(dirname "$0")"

APP=tpmsrx
APP_NAME="TPMS RX"
APP_VER="0.2"
APP_API_MIN=3
APP_VMA=${APP_VMA:-0x20000280}
OUT="${APP_NAME// /}"
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
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} \
  -Wl,--gc-sections -Wl,-Map=${OUT_DIR}/${APP}.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"

ASSETS=()
rm -f "$OUT_DIR"/*.app "$OUT_DIR"/*.elf "$OUT_DIR"/*.bin "$OUT_DIR"/*.map \
      "$OUT_DIR"/*_assets.h "$OUT_DIR"/*_assets.bin

step() { printf '\r  🔨 %-13s [%d/4] %-8s' "$APP_NAME" "$1" "$2"; }
trap 'printf "\r  ❌ %-13s build failed            \n" "$APP_NAME"' ERR

step 1 assets
if [ -f gen_assets.py ]; then
    SDK="$SDK" "$PYTHON" ./gen_assets.py "${OUT_DIR}/${APP}_assets.bin" "${OUT_DIR}/${APP}_assets.h"
    ASSETS=(--assets "${OUT_DIR}/${APP}_assets.bin")
fi
step 2 compile ; "$CC" $CFLAGS $LDFLAGS "${APP}_app.c" -lgcc -o "${OUT_DIR}/${APP}.elf"
step 3 objcopy ; "$OBJCOPY" -O binary "${OUT_DIR}/${APP}.elf" "${OUT_DIR}/${APP}.bin"
step 4 pack    ; "$PYTHON" "$SDK/pack_app.py" "${OUT_DIR}/${APP}.bin" "${OUT_DIR}/${OUT}.app" \
                   --name "$APP_NAME" --ver "$APP_VER" --api-min "$APP_API_MIN" \
                   --vma "${APP_VMA}" --shortcut none ${ASSETS[@]+"${ASSETS[@]}"} >/dev/null
trap - ERR

BYTES=$(wc -c < "${OUT_DIR}/${APP}.bin")
if [ "$BYTES" -gt 4096 ]; then
    printf '\r  🚨 %-13s OVERFLOWS 4 KiB (%d B)     \n' "$APP_NAME" "$BYTES"; exit 1
fi
printf '\r  ✅ %-13s %4d B  (%d%% of 4 KiB)  ->  %s/%s.app   \n' \
       "$APP_NAME" "$BYTES" "$(( BYTES * 100 / 4096 ))" "$OUT_DIR" "$OUT"
