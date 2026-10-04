/* SPDX-License-Identifier: MIT */
#include "dma.h"

static uint32_t source = 0x12345678, destination = 0xa5a5a5a5;
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}
static void submit(void) {
    dma_submit((uint32_t)(uintptr_t)&source, (uint32_t)(uintptr_t)&destination, 4);
}

int main(void) {
    uart_puts("BOOT polling edges\n");
    uint32_t status = 0xdead, error = 0xbeef;
    require(dma_poll(&status, &error) == -2 && status == 0xdead && error == 0xbeef,
            "idle polling has no result");
    submit(); /* This scenario injects a one-shot stall. */
    require(dma_poll(0, &error) == -3 && dma_read(DMA_STATUS) == DMA_BUSY,
            "invalid polling output preserves request");
    require(dma_poll(&status, 0) == -3, "missing error output");
    /* Cross the low 32-bit timer wrap during the bounded wait. */
    mmio_write(VSOC_MTIME, UINT32_MAX - DMA_TIMEOUT_TICKS / 2);
    uint32_t start = timer_ticks();
    require(dma_poll(&status, &error) == -1 && status == DMA_BUSY && error == 0,
            "polling timeout diagnostics");
    require(timer_ticks() < start && (uint32_t)(timer_ticks() - start) >= DMA_TIMEOUT_TICKS,
            "polling deadline survives timer wrap");
    require(dma_read(DMA_STATUS) == 0 && dma_read(DMA_IRQ_PENDING) == 0 &&
            destination == 0xa5a5a5a5, "polling timeout resets stalled device");
    submit();
    require(dma_poll(&status, &error) == 0 && status == DMA_DONE && error == 0 &&
            destination == source, "polling recovery");
    require(dma_poll(&status, &error) == -2, "polling result consumed once");
    uart_puts("PASS polling API edges and timer wrap\n");
    finish(1);
}
