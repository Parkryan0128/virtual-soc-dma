/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_VIRTUAL_DMA_H
#define HW_VIRTUAL_DMA_H
#include "hw/sysbus.h"
#include "qemu/timer.h"
#define TYPE_VIRTUAL_DMA "virtual-dma"
OBJECT_DECLARE_SIMPLE_TYPE(VirtualDMAState, VIRTUAL_DMA)
struct VirtualDMAState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t src, dst, len, status, error, irq_enable, irq_pending;
    uint32_t active_src, active_dst, active_len;
    QEMUTimer *timer;
    uint64_t delay_ns;
    bool stall_next, drop_irq_next, suppress_irq;
    int irq_level;
};
#endif
