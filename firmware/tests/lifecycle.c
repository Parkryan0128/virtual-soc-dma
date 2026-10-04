/* SPDX-License-Identifier: MIT */
#include "dma.h"

static uint8_t source[1026], old_destination[1026], new_destination[1026];

static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}

static void delay(uint32_t ticks) {
    uint32_t start = timer_ticks();
    while ((uint32_t)(timer_ticks() - start) < ticks) {}
}

static void submit(uint8_t *destination) {
    require(dma_irq_submit((uint32_t)(uintptr_t)(source + 1),
                          (uint32_t)(uintptr_t)(destination + 1), 1024) == 0,
            "lifecycle submit");
}

static void verify(uint8_t *destination, int copied) {
    for (unsigned i = 1; i <= 1024; i++) {
        require(source[i] == (uint8_t)(i * 13 + 9), "source unchanged");
        require(destination[i] == (copied ? source[i] : 0xa5), "lifecycle data");
    }
    require(destination[0] == 0xa5 && destination[1025] == 0xa5, "lifecycle guards");
}

static void complete(uint8_t *destination) {
    DMAResult result;
    require(dma_irq_wait(&result) == 0 && result.status == DMA_DONE && !result.error,
            "lifecycle completion");
    verify(destination, 1);
}

int main(void) {
    uart_puts("BOOT lifecycle\n");
    for (unsigned i = 0; i < 1026; i++) {
        source[i] = (uint8_t)(i * 13 + 9);
        old_destination[i] = new_destination[i] = 0xa5;
    }
    dma_irq_init();
    require(dma_irq_cancel() == -1, "cancel idle");
    DMAResult result;

    submit(old_destination);
    require(dma_read(DMA_STATUS) == DMA_BUSY && dma_irq_cancel() == 0, "abort busy");
    require(dma_read(DMA_SRC) == (uint32_t)(uintptr_t)(source + 1) &&
            dma_read(DMA_LEN) == 1024 && dma_read(DMA_IRQ_ENABLE) == 1, "abort configuration");
    require(dma_irq_wait(&result) == -2, "aborted request has no result");
    submit(new_destination);
    complete(new_destination);
    delay(20000);
    verify(old_destination, 0);
    require(dma_irq_count() == 1, "no stale abort IRQ");
    uart_puts("PASS firmware abort and reuse\n");

    submit(old_destination);
    require(dma_read(DMA_STATUS) == DMA_BUSY, "reset busy");
    dma_irq_reset();
    require(dma_read(DMA_STATUS) == 0 && dma_read(DMA_SRC) == 0 &&
            dma_read(DMA_DST) == 0 && dma_read(DMA_LEN) == 0 &&
            dma_read(DMA_IRQ_PENDING) == 0 && dma_read(DMA_IRQ_ENABLE) == 1, "reset configuration");
    require(dma_irq_wait(&result) == -2, "reset request has no result");
    for (unsigned i = 1; i <= 1024; i++) new_destination[i] = 0xa5;
    submit(new_destination);
    complete(new_destination);
    delay(20000);
    verify(old_destination, 0);
    require(dma_irq_count() == 2, "no stale reset IRQ");
    uart_puts("PASS firmware reset and reuse\n");

    dma_write(DMA_IRQ_ENABLE, 0);
    submit(old_destination);
    delay(20000);
    require(dma_read(DMA_STATUS) == DMA_DONE && dma_read(DMA_IRQ_PENDING) == 1 &&
            dma_irq_count() == 2, "completed while IRQ disabled");
    require(dma_irq_cancel() == -1, "cannot cancel completed request");
    dma_write(DMA_IRQ_ENABLE, 1);
    complete(old_destination);
    delay(20000);
    require(dma_irq_count() == 3 && dma_read(DMA_IRQ_PENDING) == 0, "pending IRQ delivered once");
    uart_puts("PASS firmware enable pending interrupt\n");
    uart_puts("PASS lifecycle suite\n");
    finish(1);
}
