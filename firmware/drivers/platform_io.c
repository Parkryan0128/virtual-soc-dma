/* SPDX-License-Identifier: MIT */
#include "platform_io.h"
void uart_puts(const char *s)
{
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)VSOC_UART_BASE;
    while (*s) {
        while (!(uart[5] & 0x20)) {
        }
        uart[0] = (uint8_t)*s++;
    }
}
void uart_hex(uint32_t v)
{
    char out[11] = "0x00000000";
    const char *h = "0123456789abcdef";
    for (unsigned i = 0; i < 8; i++) {
        out[9 - i] = h[(v >> (4 * i)) & 15];
    }
    uart_puts(out);
}
uint64_t timer_ticks(void)
{
    uint32_t high, low;
    /* RV32 needs two loads for mtime. Retry if the low word rolled over. */
    do {
        high = mmio_read(VSOC_MTIME + 4);
        low = mmio_read(VSOC_MTIME);
    } while (high != mmio_read(VSOC_MTIME + 4));
    return ((uint64_t)high << 32) | low;
}
void finish(int passed)
{
    mmio_write(VSOC_TEST_BASE, passed ? 0x5555 : 0x13333);
    for (;;) {
    }
}
