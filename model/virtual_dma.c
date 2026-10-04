/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/dma/virtual_dma.h"
#include "hw/dma/dma_regs.h"
#include "hw/dma/platform.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "trace.h"

static void vdma_update_irq(VirtualDMAState *s)
{
    int level = s->irq_enable && s->irq_pending && !s->suppress_irq;
    if (level != s->irq_level) {
        trace_virtual_dma_irq(level);
        s->irq_level = level;
    }
    qemu_set_irq(s->irq, level);
}

static void vdma_terminal(VirtualDMAState *s, uint32_t error)
{
    s->status = error ? DMA_ERROR : DMA_DONE;
    s->error = error;
    s->irq_pending = 1;
    if (s->drop_irq_next) {
        s->drop_irq_next = false;
        s->suppress_irq = true;
        trace_virtual_dma_fault("drop-irq");
    }
    trace_virtual_dma_result(s->status, error);
    vdma_update_irq(s);
}

static void vdma_complete(void *opaque)
{
    VirtualDMAState *s = opaque;
    g_autofree uint8_t *buffer = NULL;
    MemTxResult result;
    g_assert(s->status == DMA_BUSY);
    buffer = g_malloc(s->active_len);
    result = address_space_read(&address_space_memory, s->active_src,
                                MEMTXATTRS_UNSPECIFIED, buffer, s->active_len);
    g_assert(result == MEMTX_OK);
    result = address_space_write(&address_space_memory, s->active_dst,
                                 MEMTXATTRS_UNSPECIFIED, buffer, s->active_len);
    g_assert(result == MEMTX_OK);
    vdma_terminal(s, 0);
}

static bool vdma_in_ram(uint32_t addr, uint32_t len)
{
    return addr >= VSOC_RAM_BASE &&
           (uint64_t)addr + len <= (uint64_t)VSOC_RAM_BASE + VSOC_RAM_SIZE;
}

static void vdma_start(VirtualDMAState *s)
{
    uint32_t error = 0;
    if (s->status) {
        trace_virtual_dma_ignore(DMA_COMMAND, DMA_START, s->status);
        return;
    }
    s->active_src = s->src;
    s->active_dst = s->dst;
    s->active_len = s->len;
    trace_virtual_dma_start(s->src, s->dst, s->len);
    if (!s->len || s->len > DMA_MAX_LEN) {
        error = DMA_ERR_LEN;
    } else if (!vdma_in_ram(s->src, s->len)) {
        error = DMA_ERR_SRC;
    } else if (!vdma_in_ram(s->dst, s->len)) {
        error = DMA_ERR_DST;
    } else if ((uint64_t)s->src < (uint64_t)s->dst + s->len &&
               (uint64_t)s->dst < (uint64_t)s->src + s->len) {
        error = DMA_ERR_OVERLAP;
    }
    if (error) {
        vdma_terminal(s, error);
        return;
    }
    s->status = DMA_BUSY;
    if (s->stall_next) {
        s->stall_next = false;
        trace_virtual_dma_fault("stall");
        return;
    }
    int64_t deadline = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->delay_ns;
    trace_virtual_dma_schedule(deadline);
    timer_mod(s->timer, deadline);
}

static void vdma_clear_result(VirtualDMAState *s)
{
    s->status = s->error = s->irq_pending = 0;
    s->suppress_irq = false;
    vdma_update_irq(s);
}

static void vdma_reset(DeviceState *dev)
{
    VirtualDMAState *s = VIRTUAL_DMA(dev);
    timer_del(s->timer);
    trace_virtual_dma_cancel(DMA_RESET);
    s->src = s->dst = s->len = s->irq_enable = 0;
    s->active_src = s->active_dst = s->active_len = 0;
    vdma_clear_result(s);
}

static uint64_t vdma_read(void *opaque, hwaddr addr, unsigned size)
{
    VirtualDMAState *s = opaque;
    switch (addr) {
    case DMA_ID: return DMA_ID_VALUE;
    case DMA_VERSION: return DMA_VERSION_VALUE;
    case DMA_SRC: return s->src;
    case DMA_DST: return s->dst;
    case DMA_LEN: return s->len;
    case DMA_STATUS: return s->status;
    case DMA_ERROR_CODE: return s->error;
    case DMA_IRQ_ENABLE: return s->irq_enable;
    case DMA_IRQ_PENDING: return s->irq_pending;
    default: return 0;
    }
}

static void vdma_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned size)
{
    VirtualDMAState *s = opaque;
    uint32_t command;
    if (addr >= DMA_SRC && addr <= DMA_LEN && s->status == DMA_BUSY) {
        trace_virtual_dma_ignore(addr, value, s->status);
        return;
    }
    switch (addr) {
    case DMA_SRC: s->src = value; break;
    case DMA_DST: s->dst = value; break;
    case DMA_LEN: s->len = value; break;
    case DMA_IRQ_ENABLE:
        s->irq_enable = value & 1;
        vdma_update_irq(s);
        break;
    case DMA_COMMAND:
        command = value & 15;
        if (!command || (command & (command - 1))) {
            trace_virtual_dma_ignore(addr, value, s->status);
            break;
        }
        switch (command) {
        case DMA_START: vdma_start(s); break;
        case DMA_RESET: vdma_reset(DEVICE(s)); break;
        case DMA_ABORT:
            if (s->status == DMA_BUSY) {
                timer_del(s->timer);
                trace_virtual_dma_cancel(DMA_ABORT);
                vdma_clear_result(s);
            }
            break;
        case DMA_ACK:
            if (s->status == DMA_DONE || s->status == DMA_ERROR) {
                vdma_clear_result(s);
            }
            break;
        }
        break;
    default: break;
    }
}

static const MemoryRegionOps vdma_ops = {
    .read = vdma_read,
    .write = vdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4, .unaligned = false },
    .impl = { .min_access_size = 4, .max_access_size = 4, .unaligned = false },
};

static void vdma_init(Object *obj)
{
    VirtualDMAState *s = VIRTUAL_DMA(obj);
    memory_region_init_io(&s->mmio, obj, &vdma_ops, s, TYPE_VIRTUAL_DMA,
                         VSOC_DMA_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vdma_complete, s);
}

static void vdma_finalize(Object *obj)
{
    VirtualDMAState *s = VIRTUAL_DMA(obj);
    timer_free(s->timer);
}

static void vdma_realize(DeviceState *dev, Error **errp)
{
    VirtualDMAState *s = VIRTUAL_DMA(dev);
    if (!s->delay_ns || s->delay_ns > 1000000000) {
        error_setg(errp, "delay-ns must be 1..1000000000");
    }
}

static const Property vdma_properties[] = {
    DEFINE_PROP_UINT64("delay-ns", VirtualDMAState, delay_ns, 1000000),
    DEFINE_PROP_BOOL("stall-next", VirtualDMAState, stall_next, false),
    DEFINE_PROP_BOOL("drop-irq-next", VirtualDMAState, drop_irq_next, false),
};
static const VMStateDescription vdma_vmstate = {
    .name = TYPE_VIRTUAL_DMA,
    .unmigratable = true,
};
static void vdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Educational single-channel virtual DMA";
    dc->user_creatable = false;
    dc->realize = vdma_realize;
    dc->vmsd = &vdma_vmstate;
    device_class_set_props(dc, vdma_properties);
    device_class_set_legacy_reset(dc, vdma_reset);
}

static const TypeInfo vdma_info = {
    .name = TYPE_VIRTUAL_DMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtualDMAState),
    .instance_init = vdma_init,
    .instance_finalize = vdma_finalize,
    .class_init = vdma_class_init,
};
static void vdma_register(void) { type_register_static(&vdma_info); }
type_init(vdma_register)
