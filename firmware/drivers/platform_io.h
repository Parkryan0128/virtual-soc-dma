/* SPDX-License-Identifier: MIT */
#ifndef VSOC_IO_H
#define VSOC_IO_H
#include <stdint.h>
#include "platform.h"
static inline uint32_t mmio_read(uint32_t a) { uint32_t v = *(volatile uint32_t *)(uintptr_t)a; __asm__ volatile("fence iorw, iorw" ::: "memory"); return v; }
static inline void mmio_write(uint32_t a, uint32_t v) { __asm__ volatile("fence iorw, iorw" ::: "memory"); *(volatile uint32_t *)(uintptr_t)a = v; __asm__ volatile("fence iorw, iorw" ::: "memory"); }
void uart_puts(const char *s);
void uart_hex(uint32_t v);
uint32_t timer_ticks(void);
void finish(int passed) __attribute__((noreturn));
#endif
