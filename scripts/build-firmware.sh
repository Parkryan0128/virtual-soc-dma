#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/firmware
compiler=${RISCV_CC:-riscv64-unknown-elf-gcc}
for scenario in boot detect polling interrupt lifecycle cancel_race stall dropirq; do
source="firmware/tests/$scenario.c"
extra=
case "$scenario" in
    stall) source=firmware/tests/recovery.c; extra=-DSTALL_FAULT=1 ;;
    dropirq) source=firmware/tests/recovery.c; extra=-DSTALL_FAULT=0 ;;
esac
"$compiler" $extra -march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany -msmall-data-limit=0 \
    -O2 -g -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iplatform -Ifirmware/drivers -T firmware/linker.ld \
    firmware/startup.S firmware/traps.S firmware/drivers/dma.c firmware/drivers/platform_io.c "$source" \
    -Wl,--build-id=none -o "build/firmware/$scenario.elf"
done
