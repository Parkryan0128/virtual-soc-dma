/* SPDX-License-Identifier: MIT */
#ifndef VSOC_DRIVER_DMA_H
#define VSOC_DRIVER_DMA_H
#include <stdint.h>
#include "platform_io.h"
#include "dma_regs.h"
#define DMA_TIMEOUT_TICKS 100000u
static inline uint32_t dma_read(unsigned reg) { return mmio_read(VSOC_DMA_BASE + reg); }
static inline void dma_write(unsigned reg, uint32_t value) { mmio_write(VSOC_DMA_BASE + reg, value); }
static inline void dma_submit(uint32_t src, uint32_t dst, uint32_t len) {
    dma_write(DMA_SRC, src); dma_write(DMA_DST, dst); dma_write(DMA_LEN, len); dma_write(DMA_COMMAND, DMA_START);
}
static inline int dma_poll(uint32_t *status, uint32_t *error) {
    uint32_t start = timer_ticks();
    while (dma_read(DMA_STATUS) == DMA_BUSY) {
        if ((uint32_t)(timer_ticks() - start) >= DMA_TIMEOUT_TICKS) return -1;
    }
    *status = dma_read(DMA_STATUS); *error = dma_read(DMA_ERROR_CODE);
    dma_write(DMA_COMMAND, DMA_ACK);
    return 0;
}
typedef struct DMAResult { uint32_t status, error, pending; } DMAResult;
void dma_irq_init(void);
void dma_irq_reset(void);
int dma_irq_cancel(void);
int dma_irq_submit(uint32_t src, uint32_t dst, uint32_t len);
int dma_irq_wait(DMAResult *result);
uint32_t dma_irq_count(void);
#endif
