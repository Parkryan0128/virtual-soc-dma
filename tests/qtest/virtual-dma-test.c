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
    qtest_writel(q, VSOC_DMA_BASE + DMA_VERSION, 0xffffffff);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_VERSION), ==, DMA_VERSION_VALUE);
    const unsigned readonly[] = {DMA_STATUS, DMA_ERROR_CODE, DMA_IRQ_PENDING};
    for (unsigned i = 0; i < G_N_ELEMENTS(readonly); i++) {
        qtest_writel(q, VSOC_DMA_BASE + readonly[i], 0xffffffff);
        g_assert_cmpuint(qtest_readl(q, VSOC_DMA_BASE + readonly[i]), ==, 0);
    }
    qtest_writel(q, VSOC_DMA_BASE + 0x28, 0xffffffff);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + 0x28), ==, 0);
    qtest_writel(q, VSOC_DMA_BASE + DMA_SRC, VSOC_RAM_BASE);
    qtest_writeb(q, VSOC_DMA_BASE + DMA_SRC, 0);
    qtest_writew(q, VSOC_DMA_BASE + DMA_SRC, 0);
    qtest_writel(q, VSOC_DMA_BASE + DMA_SRC + 1, 0);
    qtest_writeb(q, VSOC_DMA_BASE + DMA_COMMAND, DMA_RESET);
    qtest_writew(q, VSOC_DMA_BASE + DMA_COMMAND, DMA_RESET);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_SRC), ==, VSOC_RAM_BASE);
    qtest_writel(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE, 0xffffffff);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE), ==, 1);
    qtest_writel(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE, 0xfffffffe);
    g_assert_cmphex(qtest_readl(q, VSOC_DMA_BASE + DMA_IRQ_ENABLE), ==, 0);
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
        {VSOC_RAM_BASE - 1, b, 1, DMA_ERR_SRC}, {a, VSOC_RAM_BASE - 1, 1, DMA_ERR_DST},
        {end, b, 1, DMA_ERR_SRC}, {a, end, 1, DMA_ERR_DST},
        {a, b, UINT32_MAX, DMA_ERR_LEN}, {0, 0, 4, DMA_ERR_SRC},
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
    const uint32_t lengths[] = {1, 3, DMA_MAX_LEN};
    g_autofree uint8_t *before = g_malloc(DMA_MAX_LEN);
    g_autofree uint8_t *after = g_malloc(DMA_MAX_LEN);
    for (unsigned i = 0; i < DMA_MAX_LEN; i++) { before[i] = i * 37 + 5; }
    for (unsigned i = 0; i < G_N_ELEMENTS(lengths); i++) {
        uint32_t len = lengths[i], end = VSOC_RAM_BASE + VSOC_RAM_SIZE;
        const uint32_t ranges[][2] = {
            {VSOC_RAM_BASE, end - len}, {end - len, VSOC_RAM_BASE},
            {VSOC_RAM_BASE, VSOC_RAM_BASE + len},
            {VSOC_RAM_BASE + len, VSOC_RAM_BASE},
        };
        for (unsigned j = 0; j < G_N_ELEMENTS(ranges); j++) {
            uint32_t src = ranges[j][0], dst = ranges[j][1];
            qtest_memset(q, dst, 0xa5, len);
            qtest_memwrite(q, src, before, len);
            submit(q, src, dst, len); qtest_clock_step(q, 1000000);
            g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
            qtest_memread(q, dst, after, len);
            g_assert_cmpmem(after, len, before, len);
            qtest_memread(q, src, after, len);
            g_assert_cmpmem(after, len, before, len);
            put(q, DMA_COMMAND, DMA_ACK);
        }
    }
    qtest_quit(q);
}
static void test_commands(void)
{
    QTestState *q = new_dma("");
    uint32_t a = VSOC_RAM_BASE + 0x10000, b = VSOC_RAM_BASE + 0x20000;
    qtest_writel(q, a, 0x11111111); qtest_writel(q, b, 0);
    submit(q, a, b, 4);
    /* A repeated START halfway through must not postpone completion. */
    qtest_clock_step(q, 500000);
    put(q, DMA_SRC, 0); put(q, DMA_DST, 0); put(q, DMA_LEN, 0);
    put(q, DMA_COMMAND, DMA_START); put(q, DMA_COMMAND, DMA_ACK);
    put(q, DMA_COMMAND, DMA_RESET | DMA_ABORT);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
    g_assert_cmphex(reg(q, DMA_SRC), ==, a);
    g_assert_cmphex(reg(q, DMA_DST), ==, b);
    g_assert_cmpuint(reg(q, DMA_LEN), ==, 4);
    /* The source is read at completion, rather than frozen at START. */
    qtest_writel(q, a, 0x22222222);
    qtest_clock_step(q, 499999);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 0);
    g_assert_cmphex(qtest_readl(q, b), ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
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

static void test_command_matrix(void)
{
    QTestState *q = new_dma("");
    const uint32_t states[] = {0, DMA_BUSY, DMA_DONE, DMA_ERROR};
    const uint32_t a = VSOC_RAM_BASE + 0x10000, b = a + 0x10000;
    for (unsigned i = 0; i < G_N_ELEMENTS(states); i++) {
        for (uint32_t command = 0; command < 16; command++) {
            uint32_t state = states[i];
            put(q, DMA_COMMAND, DMA_RESET);
            qtest_writel(q, a, 0x12345678); qtest_writel(q, b, 0xa5a5a5a5);
            put(q, DMA_IRQ_ENABLE, 1);
            put(q, DMA_SRC, a); put(q, DMA_DST, state == DMA_ERROR ? 0 : b); put(q, DMA_LEN, 4);
            if (state) { put(q, DMA_COMMAND, DMA_START); }
            if (state == DMA_DONE) { qtest_clock_step(q, 1000000); }
            /* Reserved command bits never change the recognized command. */
            put(q, DMA_COMMAND, command | 0x80000000u);
            g_assert_cmpuint(reg(q, DMA_COMMAND), ==, 0);
            uint32_t expected = state;
            if (command == DMA_RESET || (command == DMA_ABORT && state == DMA_BUSY) ||
                (command == DMA_ACK && (state == DMA_DONE || state == DMA_ERROR))) {
                expected = 0;
            } else if (command == DMA_START && state == 0) {
                expected = DMA_BUSY;
            }
            g_assert_cmpuint(reg(q, DMA_STATUS), ==, expected);
            g_assert_cmpuint(reg(q, DMA_ERROR_CODE), ==, expected == DMA_ERROR ? DMA_ERR_DST : 0);
            g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, expected == DMA_DONE || expected == DMA_ERROR);
            g_assert_cmpuint(qtest_get_irq(q, 0), ==, expected == DMA_DONE || expected == DMA_ERROR);
            g_assert_cmpuint(reg(q, DMA_IRQ_ENABLE), ==, command != DMA_RESET);
            g_assert_cmphex(reg(q, DMA_SRC), ==, command == DMA_RESET ? 0 : a);
            g_assert_cmphex(reg(q, DMA_DST), ==, command == DMA_RESET || state == DMA_ERROR ? 0 : b);
            g_assert_cmpuint(reg(q, DMA_LEN), ==, command == DMA_RESET ? 0 : 4);
            qtest_clock_step(q, 2000000);
            g_assert_cmpuint(reg(q, DMA_STATUS), ==, expected == DMA_BUSY ? DMA_DONE : expected);
            g_assert_cmphex(qtest_readl(q, b), ==,
                            state == DMA_DONE || expected == DMA_BUSY ? 0x12345678 : 0xa5a5a5a5);
        }
    }
    qtest_quit(q);
}

static void test_deadline_cancel(void)
{
    QTestState *q = new_dma("");
    const uint32_t commands[] = {DMA_ABORT, DMA_RESET};
    const uint32_t a = VSOC_RAM_BASE + 0x10000, b = a + 0x10000;
    qtest_writel(q, a, 0x87654321);
    for (unsigned c = 0; c < G_N_ELEMENTS(commands); c++) {
        for (unsigned completed = 0; completed < 2; completed++) {
            put(q, DMA_COMMAND, DMA_RESET); put(q, DMA_IRQ_ENABLE, 1);
            qtest_writel(q, b, 0xa5a5a5a5);
            submit(q, a, b, 4);
            qtest_clock_step(q, completed ? 1000000 : 999999);
            put(q, DMA_COMMAND, commands[c]);
            qtest_clock_step(q, 2000000);
            g_assert_cmphex(qtest_readl(q, b), ==, completed ? 0x87654321 : 0xa5a5a5a5);
            bool retained = completed && commands[c] == DMA_ABORT;
            g_assert_cmpuint(reg(q, DMA_STATUS), ==, retained ? DMA_DONE : 0);
            g_assert_cmpuint(qtest_get_irq(q, 0), ==, retained);
        }
    }
    qtest_quit(q);
}

static void test_system_reset(void)
{
    QTestState *q = new_dma("");
    const uint32_t states[] = {DMA_BUSY, DMA_DONE, DMA_ERROR};
    const uint32_t a = VSOC_RAM_BASE + 0x10000, b = a + 0x10000;
    qtest_writel(q, a, 0x11223344);
    for (unsigned i = 0; i < G_N_ELEMENTS(states); i++) {
        qtest_writel(q, b, 0xa5a5a5a5); put(q, DMA_IRQ_ENABLE, 1);
        submit(q, a, states[i] == DMA_ERROR ? 0 : b, 4);
        if (states[i] == DMA_DONE) { qtest_clock_step(q, 1000000); }
        qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
        for (unsigned offset = DMA_SRC; offset <= DMA_IRQ_PENDING; offset += 4) {
            g_assert_cmpuint(reg(q, offset), ==, 0);
        }
        g_assert_cmphex(reg(q, DMA_ID), ==, DMA_ID_VALUE);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_clock_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, b), ==, states[i] == DMA_DONE ? 0x11223344 : 0xa5a5a5a5);
        submit(q, a, b + 4, 4); qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, b + 4), ==, 0x11223344);
        put(q, DMA_COMMAND, DMA_ACK);
    }
    qtest_quit(q);
}

static void test_delay_bounds(void)
{
    const uint64_t delays[] = {1, 1000000000};
    for (unsigned i = 0; i < G_N_ELEMENTS(delays); i++) {
        g_autofree char *options = g_strdup_printf("-global virtual-dma.delay-ns=%" PRIu64, delays[i]);
        QTestState *q = new_dma(options);
        submit(q, VSOC_RAM_BASE, VSOC_RAM_BASE + 0x10000, 4);
        if (delays[i] > 1) { qtest_clock_step(q, delays[i] - 1); }
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
        qtest_clock_step(q, 1);
        g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
        qtest_quit(q);
    }
}

static void test_combined_faults(void)
{
    QTestState *q = new_dma("-global virtual-dma.stall-next=on -global virtual-dma.drop-irq-next=on");
    const uint32_t a = VSOC_RAM_BASE + 0x10000, b = a + 0x10000;
    qtest_writel(q, a, 0x10203040); qtest_writel(q, b, 0xa5a5a5a5);
    put(q, DMA_COMMAND, DMA_RESET); put(q, DMA_IRQ_ENABLE, 1);
    submit(q, a, b, 4); qtest_clock_step(q, 2000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_BUSY);
    put(q, DMA_COMMAND, DMA_ABORT);
    g_assert_cmphex(qtest_readl(q, b), ==, 0xa5a5a5a5);
    /* Aborting a stall is not a terminal event, so drop-next is still armed. */
    submit(q, a, b, 4); qtest_clock_step(q, 1000000);
    g_assert_cmpuint(reg(q, DMA_STATUS), ==, DMA_DONE);
    g_assert_cmpuint(reg(q, DMA_IRQ_PENDING), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, b), ==, 0x10203040);
    put(q, DMA_COMMAND, DMA_ACK);
    submit(q, a, b + 4, 4); qtest_clock_step(q, 1000000);
    g_assert_true(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, b + 4), ==, 0x10203040);
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
    qtest_add_func("/virtual-dma/command_matrix", test_command_matrix);
    qtest_add_func("/virtual-dma/deadline_cancel", test_deadline_cancel);
    qtest_add_func("/virtual-dma/system_reset", test_system_reset);
    qtest_add_func("/virtual-dma/delay_bounds", test_delay_bounds);
    qtest_add_func("/virtual-dma/combined_faults", test_combined_faults);
    return g_test_run();
}
