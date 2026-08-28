#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
arm_build_dir="${EQVITA_ARM_TEST_BUILD_DIR:-$repo_root/build-test-armv7}"
arm_cc="${EQVITA_ARM_CC:-arm-linux-gnueabihf-gcc}"
qemu_arm="${EQVITA_QEMU_ARM:-qemu-arm}"
equalizer_binary="$arm_build_dir/test_equalizer_apo_armv7"
dsp_binary="$arm_build_dir/test_dsp_armv7"
neon_binary="$arm_build_dir/test_dsp_neon_armv7"

if ! command -v "$arm_cc" >/dev/null 2>&1; then
  echo "error: ARM compiler '$arm_cc' was not found; install gcc-arm-linux-gnueabihf" >&2
  exit 1
fi
if ! command -v "$qemu_arm" >/dev/null 2>&1; then
  echo "error: QEMU runner '$qemu_arm' was not found; install qemu-user" >&2
  exit 1
fi
mkdir -p "$arm_build_dir"

common_flags=(
  -std=c11
  -O3
  -Wall
  -Wextra
  -Werror
  -static
  -mcpu=cortex-a9
  -mfpu=neon
  -mfloat-abi=hard
  -mthumb
  -DEQVITA_HOST_TESTS=1
  -DEQVITA_DSP_TEST_API=1
  "-DEQVITA_SOURCE_DIR=\"$repo_root\""
  -I"$repo_root"
)

"$arm_cc" "${common_flags[@]}" \
  "$repo_root/tests/test_equalizer_apo.c" \
  "$repo_root/app/equalizer_apo.c" \
  "$repo_root/plugin/dsp.c" \
  -lm \
  -o "$equalizer_binary"

"$arm_cc" "${common_flags[@]}" \
  "$repo_root/tests/test_dsp.c" \
  "$repo_root/plugin/dsp.c" \
  -lm \
  -o "$dsp_binary"

"$arm_cc" "${common_flags[@]}" \
  "$repo_root/tests/test_dsp_neon_armv7.c" \
  "$repo_root/plugin/dsp.c" \
  -lm \
  -o "$neon_binary"

"$qemu_arm" -cpu cortex-a9 "$equalizer_binary"
"$qemu_arm" -cpu cortex-a9 "$dsp_binary"
"$qemu_arm" -cpu cortex-a9 "$neon_binary"
