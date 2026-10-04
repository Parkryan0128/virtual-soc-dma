/* SPDX-License-Identifier: MIT */
#include "dma.h"
#ifndef STALL_FAULT
#define STALL_FAULT 0
#endif
static uint8_t source[1026], destination[1026], fresh_source[1026], fresh_destination[1026];
static void require(int condition, const char *label) {
    if (!condition) { uart_puts("FAIL "); uart_puts(label); uart_puts("\n"); finish(0); }
}
int main(void) {
    const char *name = STALL_FAULT ? "stall" : "dropirq";
    uart_puts("BOOT "); uart_puts(name); uart_puts("\n");
    for (unsigned i=0; i<1026; i++) { source[i]=(uint8_t)(i*7+3); destination[i]=0xa5; fresh_source[i]=(uint8_t)(i*11+19); fresh_destination[i]=0x5a; }
    dma_irq_init(); DMAResult result;
    require(dma_irq_submit((uint32_t)(uintptr_t)(source+1), (uint32_t)(uintptr_t)(destination+1), 1024) == 0, "fault submit");
    require(dma_irq_wait(&result) == -1, "expected driver timeout");
    uart_puts("TIMEOUT status="); uart_hex(result.status); uart_puts(" error="); uart_hex(result.error); uart_puts(" pending="); uart_hex(result.pending); uart_puts("\n");
    require(result.status == (STALL_FAULT ? DMA_BUSY : DMA_DONE) && result.error == 0 && result.pending == (STALL_FAULT ? 0u : 1u), "timeout diagnostics");
    require(dma_irq_count() == 0 && dma_read(DMA_STATUS) == 0 && dma_read(DMA_IRQ_PENDING) == 0 && dma_read(DMA_IRQ_ENABLE) == 1, "reset state");
    for (unsigned i=1; i<=1024; i++) require(destination[i] == (STALL_FAULT ? 0xa5 : source[i]), "fault data state");
    require(destination[0] == 0xa5 && destination[1025] == 0xa5, "fault guards");
    uart_puts("PASS "); uart_puts(name); uart_puts(" timeout\n");
    require(dma_irq_submit((uint32_t)(uintptr_t)(fresh_source+1), (uint32_t)(uintptr_t)(fresh_destination+1), 1024) == 0, "recovery submit");
    require(dma_irq_wait(&result) == 0 && result.status == DMA_DONE && result.error == 0 && dma_irq_count() == 1, "recovery interrupt");
    uint32_t start = timer_ticks();
    while ((uint32_t)(timer_ticks() - start) < DMA_TIMEOUT_TICKS * 2) {}
    for (unsigned i=1; i<=1024; i++) {
        require(fresh_destination[i] == fresh_source[i] && fresh_source[i] == (uint8_t)(i*11+19), "recovery data");
        require(destination[i] == (STALL_FAULT ? 0xa5 : source[i]), "no late old transfer");
    }
    require(fresh_destination[0] == 0x5a && fresh_destination[1025] == 0x5a && dma_irq_count() == 1, "recovery guards and IRQ count");
    uart_puts("PASS "); uart_puts(name); uart_puts(" recovery\n"); finish(1);
}
