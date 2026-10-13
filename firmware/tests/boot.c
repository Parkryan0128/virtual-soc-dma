/* SPDX-License-Identifier: MIT */
#include "platform_io.h"
int main(void)
{
    uart_puts("BOOT RV32 M-mode\n");
    uint64_t start = timer_ticks();
    while ((timer_ticks() - start) < 100) {
    }
    uart_puts("PASS timer\nPASS boot\n");
    finish(1);
}
