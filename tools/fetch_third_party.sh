#!/bin/sh
# Download the third-party sources kept in third_party/ (already committed;
# this script documents where they came from and lets you refresh them).
#   CMSIS core headers   ARM, Apache-2.0
#   STM32WB device header ST, Apache-2.0
#   FatFs                ChaN, BSD-style licence (see ff.c)
set -eu
cd "$(dirname "$0")/../third_party"
get() { mkdir -p "$(dirname "$1")"; curl -fsS -m 120 -o "$1" "$2"; echo "ok $1"; }

ARM=https://raw.githubusercontent.com/ARM-software/CMSIS_5/5.9.0/CMSIS/Core/Include
for f in core_cm4.h cmsis_gcc.h cmsis_compiler.h cmsis_version.h mpu_armv7.h; do
    get cmsis/$f $ARM/$f
done
ST=https://raw.githubusercontent.com/STMicroelectronics/cmsis_device_wb/v1.12.2/Include
for f in stm32wb55xx.h stm32wbxx.h system_stm32wbxx.h; do
    get cmsis/$f $ST/$f
done
FF=https://raw.githubusercontent.com/abbrev/fatfs/master/source
for f in ff.c ff.h diskio.h ffunicode.c; do
    get fatfs/$f $FF/$f
done
get fatfs/LICENSE.txt https://raw.githubusercontent.com/abbrev/fatfs/master/LICENSE.txt
