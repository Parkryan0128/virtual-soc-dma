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
typedef struct DMAResult { uint32_t status, error, pending; } DMAResult;
enum {
    DMA_WAIT_OK = 0,
    DMA_WAIT_TIMEOUT = -1,
    DMA_WAIT_NO_REQUEST = -2,
    DMA_WAIT_INVALID = -3,
};
/* Polling and IRQ APIs are separate ownership modes; do not mix on a request.
 * Wait outputs are required. NO_REQUEST/INVALID leave output and device alone.
 * TIMEOUT records diagnostics and resets the device before returning. */
int dma_poll(uint32_t *status, uint32_t *error);
void dma_irq_init(void);
void dma_irq_reset(void);
int dma_irq_cancel(void);
int dma_irq_submit(uint32_t src, uint32_t dst, uint32_t len);
int dma_irq_wait(DMAResult *result);
uint32_t dma_irq_count(void);
#endif
