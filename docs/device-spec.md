# DMA device specification

Implemented device contract (register interface version 1.0). Model, driver, and tests follow these rules. Any required change must update this document before the affected milestone is accepted.

## Platform

One RV32 hart, little endian, M-mode bare metal, QEMU TCG, PLIC (`aia=none`), and exactly 128 MiB of RAM. Guest addresses are physical addresses; there is no guest virtual-memory translation or cache model.

- RAM: `[0x80000000, 0x88000000)`.
- DMA MMIO: `[0x10010000, 0x10011000)`.
- DMA interrupt: PLIC source 16.
- UART: reused platform UART at `0x10000000`.

DMA address/IRQ allocation was checked against the pinned board memory map and all existing interrupt assignments. Confirmed values are frozen in `platform/platform.h`, shared by firmware and model integration. The generated DTB must describe the same mapping; firmware may use the fixed header because the project has one fixed platform.

## Register map

Only aligned 32-bit little-endian accesses are supported. Unsupported individual bus access sizes and unaligned accesses must not mutate device state and are reported as invalid guest accesses. QTest bulk writes may be split by QEMU into multiple legal 32-bit transactions; a `writeq` command is not proof of one 64-bit bus access on this RV32 platform. Reserved aligned offsets read as zero and ignore writes. Reserved bits read as zero and ignore writes.

| Offset | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | ID | RO | `0x56444D41` identifies this educational device |
| `0x04` | VERSION | RO | `0x00010000` (v1.0) |
| `0x08` | SRC | RW | Source physical address |
| `0x0C` | DST | RW | Destination physical address |
| `0x10` | LEN | RW | Transfer byte count |
| `0x14` | COMMAND | WO | START=1, ABORT=2, ACK=4, RESET=8; reads return zero |
| `0x18` | STATUS | RO | BUSY=1, DONE=2, ERROR=4 |
| `0x1C` | ERROR_CODE | RO | 0=none, 1=length, 2=source range, 3=destination range, 4=overlap |
| `0x20` | IRQ_ENABLE | RW | Bit 0 enables completion/error IRQ |
| `0x24` | IRQ_PENDING | RO | Bit 0 is a sticky completion/error event latch |

Exactly one recognized command bit must be written. Zero or multiple recognized bits are no-ops recorded in the trace. Writes to RO registers are ignored. SRC/DST/LEN writes while BUSY are ignored. IRQ_ENABLE may change in any state and immediately updates the IRQ line.

## Transfer contract

- START is accepted only in IDLE. Requests while BUSY or in an unacknowledged terminal state are ignored and traced; they must not replace the active transfer or its result.
- Snapshot SRC/DST/LEN at accepted START. Validate in this order: length, source range, destination range, overlap.
- LEN must be 1–65,536 bytes. Byte-aligned source and destination are allowed.
- Both complete ranges must lie in RAM. Validate with widened arithmetic; address addition must not wrap.
- Source and destination must not overlap; identical addresses also produce overlap error. Device-register addresses are never valid DMA targets.
- Invalid START immediately enters ERROR, sets ERROR_CODE and IRQ_PENDING, and performs no memory access or destination write.
- Valid START enters BUSY and schedules a QEMU virtual-clock event after a host-configured delay (default: 1 ms of virtual time).
- At completion, read the source and write the destination through QEMU guest physical-memory APIs, then publish DONE and IRQ_PENDING. Read the source at completion; firmware must leave source buffers unchanged until the transfer finishes.
- Copy is a single bounded completion callback. Destination changes become visible at completion, not incrementally. Firmware must not access the destination while BUSY.
- Allocation/mapping/internal API failures are model/test failures, not silently converted into a successful guest transfer.

## State, interrupts, abort, and reset

```text
IDLE --valid START--> BUSY --completion--> DONE
IDLE --invalid START--------------------> ERROR
DONE/ERROR --ACK------------------------> IDLE
BUSY --ABORT----------------------------> IDLE
Any state --RESET----------------------> IDLE (reset registers)
```

BUSY, DONE, and ERROR are mutually exclusive. IDLE has STATUS=0. ERROR_CODE is nonzero only in ERROR.

Normal IRQ line: `IRQ_ENABLE && IRQ_PENDING`. DONE/ERROR and IRQ_PENDING remain latched until ACK or RESET. ACK in DONE/ERROR clears terminal status, error code, and pending IRQ, while preserving SRC/DST/LEN and IRQ_ENABLE. ACK in IDLE/BUSY is a no-op. The ISR must snapshot the result, ACK the DMA, then complete the PLIC claim.

ABORT in BUSY cancels completion, clears status/error/pending IRQ, and preserves configuration and IRQ_ENABLE. ABORT elsewhere is a no-op. RESET always cancels any pending completion, clears all writable registers/status/error/pending IRQ, and lowers the IRQ line. ID and VERSION remain unchanged. Neither operation copies data or raises an interrupt. A cancelled transfer must never fire later, including after another START. Re-enabling IRQ after a full reset is the driver's responsibility.

## Host-only fault injection

Faults are test options, not guest-visible registers. Set them with `-global virtual-dma.stall-next=on`, `-global virtual-dma.drop-irq-next=on`, and `-global virtual-dma.delay-ns=<nanoseconds>`. Delay must be 1–1,000,000,000 ns.

- **stall-next:** the next valid START enters BUSY without scheduling completion. Consumed once; an invalid request does not consume it.
- **drop-irq-next:** the next terminal event still sets DONE/ERROR and IRQ_PENDING but suppresses its external IRQ. Suppression lasts until ACK or RESET and is consumed once.
- **completion delay:** fixed delay in QEMU virtual time, configurable per run.

The driver interrupt path waits for a software flag set by its ISR with a timer-based deadline. A dropped IRQ must time out even though STATUS shows DONE; do not quietly switch this test to polling. On timeout, preserve diagnostics, reset, clear any stale PLIC claim/pending condition as required, reinitialize, and retry with a fresh transfer. The one-shot fault must allow that next transfer to succeed.

Firmware uses guest timer deadlines and an active bounded wait loop so time can advance during interrupt-loss/stall tests. Every host test also has a wall-clock watchdog to catch firmware hangs.

## Test matrix

| Scenario | Required observable result |
| --- | --- |
| Reset/ID | Constants correct; writable state zero; no IRQ |
| Copy 1, 3, 1024, and 65,536 bytes | Exact content; guard bytes intact; DONE; source unchanged |
| RAM boundary | Transfer ending exactly at RAM end allowed; crossing end rejected |
| Zero/excessive length, invalid source/destination, overflow, overlap | Correct first error; destination unchanged |
| Repeated START or config write while BUSY | Original snapshot completes; no second transfer |
| IRQ disabled/enabled, ACK | Pending latch and level follow the contract; no interrupt storm |
| ABORT/RESET before completion | No late copy/IRQ after advancing beyond old deadline; reuse succeeds |
| Polling firmware | Reads terminal status, validates memory, acknowledges result |
| Interrupt firmware | PLIC delivers event; ISR records result and clears source correctly |
| CPU work while BUSY | Guest task progresses before completion under configured delay |
| Stall/drop-IRQ | Bounded timeout; diagnostic recorded; reset and fresh transfer succeed |

QTest verifies registers, memory, virtual-clock events, and interrupt wiring. Firmware tests additionally verify instruction execution, driver ordering, traps, PLIC handling, deadlines, and recovery. Both are required; one cannot substitute for the other.

## Firmware driver lifecycle

The single-hart M-mode driver provides `dma_irq_submit`, `dma_irq_wait`, `dma_irq_cancel`, and `dma_irq_reset`. Calls run from foreground firmware, not from an ISR. Only one request may be outstanding. CPU interrupt masking protects changes to shared request state; volatile ISR fields and explicit memory fences publish results before the completion flag.

`dma_irq_cancel` cancels only a BUSY request. It returns -1 for idle or already completed work, preserving a completed request's result for `dma_irq_wait`. If completion wins between reading BUSY and issuing ABORT, the driver checks the post-command state and preserves that completion for wait. A successful cancel removes the outstanding request; waiting on it returns -2. `dma_irq_reset` discards any outstanding result, clears the device and driver request state, drains stale PLIC claims, and re-enables device interrupts. The cumulative IRQ counter is retained for diagnostics.

The lifecycle firmware tests cancellation and reset before completion, old/new destination buffers, no late writes/IRQs, reuse, and completion while device IRQs are disabled followed by enable-after-pending delivery. The cancellation race scenario also sweeps 128 requests around a short completion deadline and verifies both possible outcomes without losing results.
