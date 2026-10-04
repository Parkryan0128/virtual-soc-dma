/* SPDX-License-Identifier: MIT */
#include "platform_io.h"
void uart_puts(const char *s) {
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)VSOC_UART_BASE;
    while (*s) { while (!(uart[5] & 0x20)) {} uart[0] = (uint8_t)*s++; }
}
void uart_hex(uint32_t v) { char out[11] = "0x00000000"; const char *h = "0123456789abcdef"; for (unsigned i=0; i<8; i++) out[9-i] = h[(v >> (4*i)) & 15]; uart_puts(out); }
uint32_t timer_ticks(void) { return mmio_read(VSOC_MTIME); }
void finish(int passed) { mmio_write(VSOC_TEST_BASE, passed ? 0x5555 : 0x13333); for (;;) {} }
