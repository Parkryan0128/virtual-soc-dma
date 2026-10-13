/* SPDX-License-Identifier: MIT */
#include "dma.h"

static uint32_t source = 0x12345678, destination;

static void require(int condition, const char *label)
{
    if (!condition) {
        uart_puts("FAIL ");
        uart_puts(label);
        uart_puts("\n");
        finish(0);
    }
}

int main(void)
{
    DMAResult result;
    for (unsigned setup = 0; setup < 4; setup++) {
        __asm__ volatile("csrc mstatus, %0" ::"r"(8u) : "memory");
        mmio_write(VSOC_PLIC_BASE + VSOC_DMA_IRQ * 4, setup & 1);
        mmio_write(VSOC_PLIC_ENABLE, setup & 2 ? 1u << VSOC_DMA_IRQ : 0);
        mmio_write(VSOC_PLIC_THRESHOLD, 7);
        dma_write(DMA_IRQ_ENABLE, 1);
        dma_submit(0, 0, 0);
        require(mmio_read(VSOC_PLIC_BASE + 0x1000) & (1u << VSOC_DMA_IRQ),
                "old completion latched in PLIC");

        dma_irq_init();
        require(dma_irq_count() == setup &&
                    dma_irq_wait(&result) == DMA_WAIT_NO_REQUEST &&
                    dma_read(DMA_STATUS) == 0,
                "init discards old completion without ISR");
        destination = 0;
        require(dma_irq_submit((uint32_t)(uintptr_t)&source,
                               (uint32_t)(uintptr_t)&destination,
                               sizeof(source)) == 0,
                "submit after dirty init");
        require(dma_irq_wait(&result) == DMA_WAIT_OK &&
                    result.status == DMA_DONE && destination == source &&
                    dma_irq_count() == setup + 1,
                "fresh completion after dirty init");
    }
    uart_puts("PASS IRQ initialization drains masked PLIC events\n");
    finish(1);
}
