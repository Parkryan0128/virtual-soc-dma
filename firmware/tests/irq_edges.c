/* SPDX-License-Identifier: MIT */
#include "dma.h"

static uint32_t source = 0x98765432, destination;
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}
static void submit(void) {
    require(dma_irq_submit((uint32_t)(uintptr_t)&source,
                          (uint32_t)(uintptr_t)&destination, 4) == 0, "edge submit");
}
static void delay(uint32_t ticks) {
    uint32_t start = timer_ticks();
    while ((uint32_t)(timer_ticks() - start) < ticks) {}
}
static void complete(void) {
    DMAResult result;
    require(dma_irq_wait(&result) == 0 && result.status == DMA_DONE &&
            result.error == 0 && result.pending == 1 && destination == source,
            "edge completion");
    require(dma_irq_wait(&result) == -2, "IRQ result consumed once");
}

int main(void) {
    uart_puts("BOOT IRQ edges\n");
    dma_irq_init();
    DMAResult result = {0xdead, 0xbeef, 0xcafe};
    require(dma_irq_wait(&result) == -2 && result.status == 0xdead, "idle IRQ wait");
    mmio_write(VSOC_MTIME, UINT32_MAX - DMA_TIMEOUT_TICKS / 2);
    uint32_t start = timer_ticks();
    submit(); /* One-shot stall, as in the polling edge scenario. */
    require(dma_irq_wait(0) == -3 && dma_read(DMA_STATUS) == DMA_BUSY,
            "invalid IRQ output preserves request");
    require(dma_irq_wait(&result) == -1 && result.status == DMA_BUSY && !result.pending,
            "IRQ timeout diagnostics");
    require(timer_ticks() < start && (uint32_t)(timer_ticks() - start) >= DMA_TIMEOUT_TICKS,
            "IRQ deadline survives timer wrap");
    submit(); complete();
    require(dma_irq_count() == 1, "recovery ISR count");

    /* Let PLIC latch a completion while the CPU cannot enter its handler. */
    __asm__ volatile("csrc mstatus, %0" :: "r"(8u) : "memory");
    submit(); delay(20000);
    require(dma_read(DMA_STATUS) == DMA_DONE && dma_irq_count() == 1 &&
            (mmio_read(VSOC_PLIC_BASE + 0x1000) & (1u << VSOC_DMA_IRQ)), "PLIC completion pending");
    dma_irq_reset();
    uint32_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    require(!(mstatus & 8u), "reset preserves caller interrupt mask");
    __asm__ volatile("csrs mstatus, %0" :: "r"(8u) : "memory");
    delay(20000);
    require(dma_irq_count() == 1 && dma_irq_wait(&result) == -2, "reset drains stale PLIC completion");
    submit(); complete();
    require(dma_irq_count() == 2, "new IRQ after draining old PLIC claim");

    submit(); delay(20000); /* ISR ran, but foreground has not consumed it. */
    require(dma_irq_count() == 3 && dma_irq_cancel() == -1, "completed ISR cannot be cancelled");
    require(dma_irq_submit(0, 0, 0) == -1, "unconsumed ISR result blocks resubmit");
    dma_irq_reset();
    require(dma_irq_wait(&result) == -2, "reset discards completed ISR result");
    dma_irq_reset(); dma_irq_init(); /* Repeated idle reset/init must be safe. */
    submit(); complete(); delay(20000);
    require(dma_irq_count() == 4, "repeated reset no stale ISR");
    uart_puts("PASS IRQ API edges, PLIC reset and timer wrap\n");
    finish(1);
}
