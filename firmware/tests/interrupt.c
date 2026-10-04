/* SPDX-License-Identifier: MIT */
#include "dma.h"
static uint8_t source[DMA_MAX_LEN + 2], destination[DMA_MAX_LEN + 2];
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}
int main(void) {
    uart_puts("BOOT interrupt\n"); dma_irq_init();
    const unsigned sizes[] = {1, 3, 1024, DMA_MAX_LEN};
    DMAResult result;
    for (unsigned t = 0; t < 4; t++) {
        unsigned n = sizes[t];
        for (unsigned i = 0; i < n + 2; i++) { source[i] = (uint8_t)(i * 17 + t); destination[i] = 0xa5; }
        require(dma_irq_submit((uint32_t)(uintptr_t)(source + 1), (uint32_t)(uintptr_t)(destination + 1), n) == 0, "submit");
        require(dma_irq_submit(0, 0, 0) == -1, "duplicate submit");
        volatile unsigned work = 0;
        for (unsigned i = 0; i < 100; i++) work++;
        require(work == 100 && dma_read(DMA_STATUS) == DMA_BUSY, "CPU progress");
        require(dma_irq_wait(&result) == 0 && result.status == DMA_DONE && !result.error && result.pending == 1, "ISR result");
        require(dma_irq_count() == t + 1 && dma_read(DMA_STATUS) == 0 && dma_read(DMA_IRQ_PENDING) == 0, "ACK and IRQ count");
        for (unsigned i = 1; i <= n; i++) require(destination[i] == source[i] && source[i] == (uint8_t)(i * 17 + t), "content");
        require(destination[0] == 0xa5 && destination[n + 1] == 0xa5, "guards");
        uart_puts("PASS interrupt copy "); uart_hex(n); uart_puts("\n");
    }
    require(dma_irq_submit(VSOC_RAM_BASE, 0, 4) == 0, "invalid submit");
    require(dma_irq_wait(&result) == 0 && result.status == DMA_ERROR && result.error == DMA_ERR_DST, "error ISR");
    uint32_t start = timer_ticks();
    while ((uint32_t)(timer_ticks() - start) < 20000) {}
    require(dma_irq_count() == 5, "no interrupt storm");
    uart_puts("PASS interrupt suite\n"); finish(1);
}
