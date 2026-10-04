/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/dma/dma_regs.h"
#include "hw/dma/platform.h"
static void test_registers(void)
{
    QTestState *q = qtest_init("-M virt,dma=on,aia=none -cpu rv32 -m 128M -bios none");
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_ID), ==, DMA_ID_VALUE);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_VERSION), ==, DMA_VERSION_VALUE);
    for (int offset = DMA_SRC; offset <= DMA_IRQ_PENDING; offset += 4) {
        g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + offset), ==, 0);
    }
    qtest_writel(q, VSOC_DMA_BASE + DMA_ID, 0);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_ID), ==, DMA_ID_VALUE);
    qtest_writel(q, VSOC_DMA_BASE + 0x28, 0xffffffff);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + 0x28), ==, 0);
    qtest_writel(q, VSOC_DMA_BASE + DMA_SRC, VSOC_RAM_BASE);
    qtest_writeb(q, VSOC_DMA_BASE + DMA_SRC, 0);
    qtest_writew(q, VSOC_DMA_BASE + DMA_SRC, 0);
    qtest_writel(q, VSOC_DMA_BASE + DMA_SRC + 1, 0);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_SRC), ==, VSOC_RAM_BASE);
    qtest_writel(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE, 0xffffffff);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE), ==, 1);
    qtest_writel(q, VSOC_DMA_BASE + DMA_COMMAND, DMA_RESET);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE), ==, 0);
    qtest_quit(q);
}

static QTestState *new_dma(const char *options)
{
    QTestState *q = qtest_initf("-M virt,dma=on,aia=none -cpu rv32 -m 128M -bios none %s", options);
    qtest_irq_intercept_out_named(q, "/machine/virtual-dma", "sysbus-irq");
    return q;
}
static uint32_t reg(QTestState *q, unsigned offset) { return qtest_readl(q, VSOC_DMA_BASE + offset); }
static void put(QTestState *q, unsigned offset, uint32_t value) { qtest_writel(q, VSOC_DMA_BASE + offset, value); }
static void submit(QTestState *q, uint32_t src, uint32_t dst, uint32_t len)
{
    put(q, DMA_SRC, src); put(q, DMA_DST, dst); put(q, DMA_LEN, len); put(q, DMA_COMMAND, DMA_START);
}
static void test_copy(void)
{
    QTestState *q = new_dma("");
    const unsigned lengths[] = {1, 3, 1024, DMA_MAX_LEN};
    uint8_t *src = g_malloc(DMA_MAX_LEN), *dst = g_malloc(DMA_MAX_LEN + 2);
    for (unsigned i = 0; i < DMA_MAX_LEN; i++) { src[i] = i * 31 + 7; }
    for (unsigned j = 0; j < G_N_ELEMENTS(lengths); j++) {
        unsigned len = lengths[j];
        memset(dst, 0xa5, len + 2);
        qtest_memwrite(q, VSOC_RAM_BASE + 0x10001, src, len);
        qtest_memwrite(q, VSOC_RAM_BASE + 0x40000, dst, len + 2);
        submit(q, VSOC_RAM_BASE + 0x10001, VSOC_RAM_BASE + 0x40001, len);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
        qtest_clock_step(q, 999999);
        qtest_memread(q, VSOC_RAM_BASE + 0x40000, dst, len + 2);
        for (unsigned i = 0; i < len + 2; i++) { g_assert_cmphex(dst[i], ==, 0xa5); }
        qtest_clock_step(q, 1);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
        g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
        qtest_memread(q, VSOC_RAM_BASE + 0x40000, dst, len + 2);
        g_assert_cmpmem(dst + 1, len, src, len);
        g_assert_cmphex(dst[0], ==, 0xa5); g_assert_cmphex(dst[len + 1], ==, 0xa5);
        qtest_memread(q, VSOC_RAM_BASE + 0x10001, dst, len);
        g_assert_cmpmem(dst, len, src, len);
        put(q, DMA_COMMAND, DMA_ACK);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, 0);
    }
    g_free(src); g_free(dst); qtest_quit(q);
}
static void test_errors(void)
{
    QTestState *q = new_dma("");
    const uint32_t a = VSOC_RAM_BASE + 0x10000, b = VSOC_RAM_BASE + 0x20000;
    const uint32_t end = VSOC_RAM_BASE + VSOC_RAM_SIZE;
    const uint32_t cases[][4] = {
        {a, b, 0, DMA_ERR_LEN}, {a, b, DMA_MAX_LEN + 1, DMA_ERR_LEN},
        {0, b, 16, DMA_ERR_SRC}, {VSOC_DMA_BASE, b, 16, DMA_ERR_SRC},
        {a, 0, 16, DMA_ERR_DST}, {a, VSOC_DMA_BASE, 16, DMA_ERR_DST},
        {end - 8, b, 16, DMA_ERR_SRC}, {a, end - 8, 16, DMA_ERR_DST},
        {0xfffffff0, b, 32, DMA_ERR_SRC}, {a, 0xfffffff0, 32, DMA_ERR_DST},
        {a, a, 16, DMA_ERR_OVERLAP}, {a, a + 1, 16, DMA_ERR_OVERLAP},
        {a + 1, a, 16, DMA_ERR_OVERLAP}, {0, 0, 0, DMA_ERR_LEN},
    };
    uint8_t before[32], after[32]; memset(before, 0x72, sizeof(before));
    for (unsigned i = 0; i < G_N_ELEMENTS(cases); i++) {
        /* Check all potentially valid destinations, including overlap/boundary. */
        uint32_t dst = cases[i][1];
        uint32_t probe = dst >= VSOC_RAM_BASE && dst < end ? MIN(dst, end - 32) : b;
        qtest_memwrite(q, probe, before, sizeof(before));
        submit(q, cases[i][0], dst, cases[i][2]);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_ERROR);
        g_assert_cmpuint(reg(q, DMA_ERROR_CODE), ==, cases[i][3]);
        qtest_clock_step(q, 2000000);
        qtest_memread(q, probe, after, sizeof(after));
        g_assert_cmpmem(after, sizeof(after), before, sizeof(before));
        put(q, DMA_COMMAND, DMA_ACK);
        g_assert_cmpuint(reg(q, DMA_ERROR_CODE), ==, 0);
    }
    qtest_quit(q);
}
static void test_boundary(void)
{
    QTestState *q = new_dma("");
    uint32_t a = VSOC_RAM_BASE + 0x10000, last = VSOC_RAM_BASE + VSOC_RAM_SIZE - 3;
    qtest_writeb(q, a, 0x17); qtest_writeb(q, a + 1, 0x28); qtest_writeb(q, a + 2, 0x39);
    submit(q, a, last, 3); qtest_clock_step(q, 1000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_cmphex(qtest_readb(q, last + 2), ==, 0x39);
    put(q, DMA_COMMAND, DMA_ACK);
    submit(q, last, a + 16, 3); qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readb(q, a + 18), ==, 0x39);
    qtest_quit(q);
}
static void test_commands(void)
{
    QTestState *q = new_dma("");
    uint32_t a = VSOC_RAM_BASE + 0x10000, b = VSOC_RAM_BASE + 0x20000;
    qtest_writel(q, a, 0x11111111); qtest_writel(q, b, 0);
    submit(q, a, b, 4);
    put(q, DMA_SRC, 0); put(q, DMA_DST, 0); put(q, DMA_LEN, 0);
    put(q, DMA_COMMAND, DMA_START); put(q, DMA_COMMAND, DMA_ACK);
    put(q, DMA_COMMAND, DMA_RESET | DMA_ABORT);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
    g_assert_cmphex(reg(q, DMA_SRC), ==, a);
    /* The source is read at completion, rather than frozen at START. */
    qtest_writel(q, a, 0x22222222);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, b), ==, 0x22222222);
    put(q, DMA_COMMAND, DMA_START);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    put(q, DMA_STATUS, 0); put(q, DMA_ERROR_CODE, 99); put(q, DMA_IRQ_PENDING, 0);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
    put(q, DMA_COMMAND, DMA_ACK);
    put(q, DMA_COMMAND, 0); put(q, DMA_COMMAND, 0x80000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, 0);
    qtest_quit(q);
}
static void test_irq(void)
{
    QTestState *q = new_dma("");
    submit(q, VSOC_RAM_BASE, VSOC_RAM_BASE + 0x10000, 4);
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));
    put(q, DMA_IRQ_ENABLE, 1); g_assert_true(qtest_get_irq(q, 0));
    /* Verify board routing as well as the device output. */
    g_assert_cmphex(qtest_readl(q, VSOC_PLIC_BASE + 0x1000) & (1u << VSOC_DMA_IRQ), ==, 1u << VSOC_DMA_IRQ);
    put(q, DMA_IRQ_ENABLE, 0); g_assert_false(qtest_get_irq(q, 0));
    put(q, DMA_IRQ_ENABLE, 1); g_assert_true(qtest_get_irq(q, 0));
    put(q, DMA_COMMAND, DMA_ACK); g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 0);
    submit(q, VSOC_RAM_BASE, 0, 4); g_assert_true(qtest_get_irq(q, 0));
    put(q, DMA_COMMAND, DMA_RESET); g_assert_false(qtest_get_irq(q, 0));
    qtest_quit(q);
}
static void test_cancel(void)
{
    QTestState *q = new_dma("");
    uint32_t a = VSOC_RAM_BASE + 0x10000, b = VSOC_RAM_BASE + 0x20000;
    const uint32_t commands[] = {DMA_ABORT, DMA_RESET};
    qtest_writel(q, a, 0x12345678);
    for (unsigned i = 0; i < G_N_ELEMENTS(commands); i++) {
        qtest_writel(q, b, 0xa5a5a5a5); put(q, DMA_IRQ_ENABLE, 1);
        submit(q, a, b, 4); qtest_clock_step(q, 500000);
        put(q, DMA_COMMAND, commands[i]);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, 0);
        g_assert_cmpuint(reg(q, DMA_IRQ_ENABLE), ==, commands[i] == DMA_ABORT);
        /* New transfer has a later deadline than the cancelled one. */
        submit(q, a, b + 4, 4); qtest_clock_step(q, 500000);
        g_assert_cmphex(qtest_readl(q, b), ==, 0xa5a5a5a5);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_clock_step(q, 500000);
        g_assert_cmphex(qtest_readl(q, b + 4), ==, 0x12345678);
        put(q, DMA_COMMAND, DMA_ACK);
    }
    qtest_quit(q);
}


static void test_stall(void)
{
    QTestState *q = new_dma("-global virtual-dma.stall-next=on");
    uint32_t a = VSOC_RAM_BASE + 0x10000, b = VSOC_RAM_BASE + 0x20000;
    /* Invalid START must not consume the pending valid-transfer fault. */
    submit(q, a, b, 0); put(q, DMA_COMMAND, DMA_ACK);
    qtest_writel(q, a, 0x12345678); qtest_writel(q, b, 0xa5a5a5a5);
    put(q, DMA_IRQ_ENABLE, 1); submit(q, a, b, 4);
    qtest_clock_step(q, 20000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, b), ==, 0xa5a5a5a5);
    put(q, DMA_COMMAND, DMA_RESET); put(q, DMA_IRQ_ENABLE, 1);
    submit(q, a, b + 4, 4); qtest_clock_step(q, 1000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_true(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, b + 4), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(q, b), ==, 0xa5a5a5a5);
    qtest_quit(q);
}
static void test_dropirq(void)
{
    QTestState *q = new_dma("-global virtual-dma.drop-irq-next=on");
    put(q, DMA_IRQ_ENABLE, 1);
    qtest_writel(q, VSOC_RAM_BASE, 0x12345678);
    submit(q, VSOC_RAM_BASE, VSOC_RAM_BASE + 0x10000, 4);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
    g_assert_cmphex(qtest_readl(q, VSOC_RAM_BASE + 0x10000), ==, 0x12345678);
    g_assert_false(qtest_get_irq(q, 0));
    put(q, DMA_IRQ_ENABLE, 0); put(q, DMA_IRQ_ENABLE, 1);
    g_assert_false(qtest_get_irq(q, 0));
    put(q, DMA_COMMAND, DMA_ACK);
    submit(q, VSOC_RAM_BASE, VSOC_RAM_BASE + 0x20000, 4);
    qtest_clock_step(q, 1000000);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_quit(q);
}
static void test_dropirq_error(void)
{
    QTestState *q = new_dma("-global virtual-dma.drop-irq-next=on");
    put(q, DMA_IRQ_ENABLE, 1); submit(q, VSOC_RAM_BASE, 0, 4);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_ERROR);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    put(q, DMA_COMMAND, DMA_RESET); put(q, DMA_IRQ_ENABLE, 1);
    submit(q, VSOC_RAM_BASE, 0, 4);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtual-dma/registers", test_registers);
    qtest_add_func("/virtual-dma/copy", test_copy);
    qtest_add_func("/virtual-dma/errors", test_errors);
    qtest_add_func("/virtual-dma/boundary", test_boundary);
    qtest_add_func("/virtual-dma/commands", test_commands);
    qtest_add_func("/virtual-dma/irq", test_irq);
    qtest_add_func("/virtual-dma/cancel", test_cancel);
    qtest_add_func("/virtual-dma/stall", test_stall);
    qtest_add_func("/virtual-dma/dropirq", test_dropirq);
    qtest_add_func("/virtual-dma/dropirq_error", test_dropirq_error);
    return g_test_run();
}
