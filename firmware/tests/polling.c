/* SPDX-License-Identifier: MIT */
#include "dma.h"
static uint8_t source[DMA_MAX_LEN + 2], destination[DMA_MAX_LEN + 2];
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}
int main(void) {
    uart_puts("BOOT polling\n");
    const unsigned sizes[] = {1, 3, 1024, DMA_MAX_LEN};
    uint32_t status, error;
    for (unsigned t = 0; t < 4; t++) {
        unsigned n = sizes[t];
        for (unsigned i = 0; i < n + 2; i++) { source[i] = (uint8_t)(i * 31 + 7); destination[i] = 0xa5; }
        dma_submit((uint32_t)(uintptr_t)(source + 1), (uint32_t)(uintptr_t)(destination + 1), n);
        require(dma_read(DMA_STATUS) == DMA_BUSY, "busy");
        volatile uint32_t work = 0;
        for (unsigned i = 0; i < 100; i++) work++;
        require(work == 100 && dma_read(DMA_STATUS) == DMA_BUSY, "CPU progress");
        require(dma_poll(&status, &error) == 0 && status == DMA_DONE && error == 0, "completion");
        for (unsigned i = 1; i <= n; i++) require(destination[i] == source[i] && source[i] == (uint8_t)(i * 31 + 7), "content");
        require(destination[0] == 0xa5 && destination[n + 1] == 0xa5, "guards");
        uart_puts("PASS polling copy "); uart_hex(n); uart_puts("\n");
    }
    dma_submit((uint32_t)(uintptr_t)source, (uint32_t)(uintptr_t)destination, 0);
    require(dma_poll(&status, &error) == 0 && status == DMA_ERROR && error == DMA_ERR_LEN, "length error");
    uart_puts("PASS polling suite\n"); finish(1);
}
