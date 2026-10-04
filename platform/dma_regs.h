#ifndef VSOC_DMA_REGS_H
#define VSOC_DMA_REGS_H
#define DMA_ID 0x00
#define DMA_VERSION 0x04
#define DMA_SRC 0x08
#define DMA_DST 0x0c
#define DMA_LEN 0x10
#define DMA_COMMAND 0x14
#define DMA_STATUS 0x18
#define DMA_ERROR_CODE 0x1c
#define DMA_IRQ_ENABLE 0x20
#define DMA_IRQ_PENDING 0x24
#define DMA_ID_VALUE 0x56444d41u
#define DMA_VERSION_VALUE 0x00010000u
#define DMA_START 1u
#define DMA_ABORT 2u
#define DMA_ACK 4u
#define DMA_RESET 8u
#define DMA_BUSY 1u
#define DMA_DONE 2u
#define DMA_ERROR 4u
#define DMA_ERR_LEN 1u
#define DMA_ERR_SRC 2u
#define DMA_ERR_DST 3u
#define DMA_ERR_OVERLAP 4u
#define DMA_MAX_LEN 65536u
#endif
