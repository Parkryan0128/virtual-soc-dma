/* SPDX-License-Identifier: MIT */
#include "platform_io.h"
#include "dma_regs.h"
int main(void) {
    uart_puts("BOOT RV32 M-mode\n");
    if (mmio_read(VSOC_DMA_BASE + DMA_ID) != DMA_ID_VALUE) finish(0);
    uart_puts("PASS DMA detection\n");
    finish(1);
}
