/* SPDX-License-Identifier: MIT */
#include "dma.h"
static volatile uint32_t irq_done, irq_status, irq_error, irq_pending, irq_count;
static uint32_t submitted_at;
static int outstanding;
extern void trap_entry(void);

static uint32_t interrupt_lock(void) {
    uint32_t previous;
    __asm__ volatile("csrrc %0, mstatus, %1" : "=r"(previous) : "r"(8u) : "memory");
    return previous;
}
static void interrupt_restore(uint32_t previous) {
    if (previous & 8u) __asm__ volatile("csrs mstatus, %0" :: "r"(8u) : "memory");
}

void dma_irq_init(void) {
    interrupt_lock();
    dma_write(DMA_COMMAND, DMA_RESET);
    irq_done = 0; outstanding = 0;
    /* Reset lowers the source; drain any old PLIC claim before enabling it. */
    uint32_t claim;
    while ((claim = mmio_read(VSOC_PLIC_CLAIM)) != 0) mmio_write(VSOC_PLIC_CLAIM, claim);
    mmio_write(VSOC_PLIC_BASE + VSOC_DMA_IRQ * 4, 1);
    mmio_write(VSOC_PLIC_ENABLE, 1u << VSOC_DMA_IRQ);
    mmio_write(VSOC_PLIC_THRESHOLD, 0);
    __asm__ volatile("csrw mtvec, %0" :: "r"((uintptr_t)trap_entry) : "memory");
    __asm__ volatile("csrs mie, %0" :: "r"(1u << 11) : "memory");
    dma_write(DMA_IRQ_ENABLE, 1);
    __asm__ volatile("csrs mstatus, %0" :: "r"(8u) : "memory");
}

int dma_irq_submit(uint32_t src, uint32_t dst, uint32_t len) {
    uint32_t previous = interrupt_lock();
    if (outstanding || dma_read(DMA_STATUS) != 0) { interrupt_restore(previous); return -1; }
    irq_done = 0; outstanding = 1; submitted_at = timer_ticks();
    dma_submit(src, dst, len);
    interrupt_restore(previous);
    return 0;
}

int dma_irq_wait(DMAResult *result) {
    if (!outstanding) return -2;
    while (!irq_done) {
        if ((uint32_t)(timer_ticks() - submitted_at) >= DMA_TIMEOUT_TICKS) {
            uint32_t previous = interrupt_lock();
            /* Do not misreport a completion delivered at the deadline. */
            if (irq_done) { interrupt_restore(previous); break; }
            result->status = dma_read(DMA_STATUS);
            result->error = dma_read(DMA_ERROR_CODE);
            result->pending = dma_read(DMA_IRQ_PENDING);
            dma_write(DMA_COMMAND, DMA_RESET);
            uint32_t claim;
            while ((claim = mmio_read(VSOC_PLIC_CLAIM)) != 0) mmio_write(VSOC_PLIC_CLAIM, claim);
            outstanding = 0; irq_done = 0;
            dma_write(DMA_IRQ_ENABLE, 1);
            interrupt_restore(previous);
            return -1;
        }
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    result->status = irq_status; result->error = irq_error; result->pending = irq_pending;
    outstanding = 0;
    return 0;
}

uint32_t dma_irq_count(void) { return irq_count; }

void trap_handler(void) {
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    if (cause != 0x8000000bu) {
        uart_puts("FAIL unexpected trap "); uart_hex(cause); uart_puts("\n"); finish(0);
    }
    uint32_t claim = mmio_read(VSOC_PLIC_CLAIM);
    if (claim != VSOC_DMA_IRQ || !outstanding) {
        uart_puts("FAIL unexpected interrupt\n"); finish(0);
    }
    irq_status = dma_read(DMA_STATUS); irq_error = dma_read(DMA_ERROR_CODE);
    irq_pending = dma_read(DMA_IRQ_PENDING);
    dma_write(DMA_COMMAND, DMA_ACK);
    mmio_write(VSOC_PLIC_CLAIM, claim);
    irq_count++;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    irq_done = 1;
}
