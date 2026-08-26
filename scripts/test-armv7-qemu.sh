#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
arm_build_dir="${EQVITA_ARM_TEST_BUILD_DIR:-$repo_root/build-test-armv7}"
arm_cc="${EQVITA_ARM_CC:-arm-linux-gnueabihf-gcc}"
qemu_arm="${EQVITA_QEMU_ARM:-qemu-arm}"
test_binary="$arm_build_dir/test_equalizer_apo_armv7"

command -v "$arm_cc" >/dev/null
command -v "$qemu_arm" >/dev/null
mkdir -p "$arm_build_dir"

"$arm_cc" \
  -std=c11 \
  -O3 \
  -Wall \
  -Wextra \
  -Werror \
  -static \
  -mcpu=cortex-a9 \
  -mfpu=neon \
  -mfloat-abi=hard \
  -mthumb \
  -DEQVITA_HOST_TESTS=1 \
  "-DEQVITA_SOURCE_DIR=\"$repo_root\"" \
  -I"$repo_root" \
  "$repo_root/tests/test_equalizer_apo.c" \
  "$repo_root/app/equalizer_apo.c" \
  "$repo_root/plugin/dsp.c" \
  -lm \
  -o "$test_binary"

"$qemu_arm" -cpu cortex-a9 "$test_binary"
