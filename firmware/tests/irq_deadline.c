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

static void delay(uint32_t ticks)
{
    uint32_t start = timer_ticks();
    while ((uint32_t)(timer_ticks() - start) < ticks) {
    }
}

static void submit(void)
{
    require(dma_irq_submit((uint32_t)(uintptr_t)&source,
                           (uint32_t)(uintptr_t)&destination,
                           sizeof(source)) == 0,
            "deadline submit");
}

int main(void)
{
    DMAResult result;
    dma_irq_init();

    /* Consuming a timely ISR result later must still succeed. */
    submit();
    delay(DMA_TIMEOUT_TICKS * 2);
    require(dma_irq_count() == 1 && dma_irq_wait(&result) == DMA_WAIT_OK &&
                result.status == DMA_DONE && destination == source,
            "timely interrupt survives delayed wait");

    for (unsigned wrap = 0; wrap < 2; wrap++) {
        dma_write(DMA_IRQ_ENABLE, 0);
        if (wrap) {
            mmio_write(VSOC_MTIME, UINT32_MAX - DMA_TIMEOUT_TICKS / 2);
        }
        submit();
        delay(DMA_TIMEOUT_TICKS * 2);
        require(dma_read(DMA_STATUS) == DMA_DONE &&
                    dma_read(DMA_IRQ_PENDING) == 1,
                "completion waiting for IRQ");
        uint32_t before = dma_irq_count();
        dma_write(DMA_IRQ_ENABLE, 1);
        delay(1000);
        require(dma_irq_count() == before + 1,
                "late ISR delivered before wait");
        require(dma_irq_wait(&result) == DMA_WAIT_TIMEOUT,
                "late interrupt must not override submission deadline");
        require(result.status == DMA_DONE && result.error == 0 &&
                    result.pending == 1,
                "late interrupt retains terminal diagnostics");
        require(dma_read(DMA_STATUS) == 0 && dma_read(DMA_IRQ_PENDING) == 0 &&
                    dma_read(DMA_IRQ_ENABLE) == 1 &&
                    dma_irq_wait(&result) == DMA_WAIT_NO_REQUEST,
                "late interrupt timeout resets request");
        submit();
        require(dma_irq_wait(&result) == DMA_WAIT_OK &&
                    result.status == DMA_DONE,
                "reuse after late interrupt");
    }
    uart_puts("PASS IRQ submission deadline and delayed wait\n");
    finish(1);
}
