/* SPDX-License-Identifier: MIT */
#include "dma.h"

static uint8_t source[18], destination[18];
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}

int main(void) {
    uart_puts("BOOT cancel race\n");
    dma_irq_init();
    unsigned cancelled = 0, completed = 0;
    for (unsigned iteration = 0; iteration < 128; iteration++) {
        for (unsigned i = 0; i < 18; i++) {
            source[i] = (uint8_t)(iteration * 19 + i);
            destination[i] = 0xa5;
        }
        dma_write(DMA_IRQ_ENABLE, 0);
        require(dma_irq_submit((uint32_t)(uintptr_t)(source + 1),
                              (uint32_t)(uintptr_t)(destination + 1), 16) == 0, "race submit");
        /* The host uses a short device delay. Sweep cancellation around its
         * boundary while withholding IRQ delivery, then resolve the result. */
        for (unsigned i = 0; i < iteration * 4; i++) __asm__ volatile("nop");
        int result = dma_irq_cancel();
        DMAResult completion;
        if (result == 0) {
            cancelled++;
            require(dma_irq_wait(&completion) == -2 && dma_read(DMA_STATUS) == 0,
                    "cancel owns no result");
            for (unsigned i = 1; i <= 16; i++) require(destination[i] == 0xa5, "cancel prevents copy");
        } else {
            completed++;
            require(dma_read(DMA_STATUS) == DMA_DONE, "completion won cancellation");
            dma_write(DMA_IRQ_ENABLE, 1);
            require(dma_irq_wait(&completion) == 0 && completion.status == DMA_DONE,
                    "completed request retained for wait");
            for (unsigned i = 1; i <= 16; i++) require(destination[i] == source[i], "completed content");
        }
        require(destination[0] == 0xa5 && destination[17] == 0xa5, "race guard bytes");
        require(dma_irq_count() == completed, "no stale cancelled interrupts");
    }
    require(cancelled > 0 && completed > 0, "both cancellation outcomes exercised");
    uart_puts("PASS cancel race cancelled="); uart_hex(cancelled);
    uart_puts(" completed="); uart_hex(completed); uart_puts("\n");
    uart_puts("PASS cancellation race suite\n");
    finish(1);
}
