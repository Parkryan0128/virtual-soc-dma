# Completion checklist

This maps the agreed project scope to executable implementation and verification. The interface version is a device protocol identifier, not a statement that only part of the implementation plan is delivered.

| Requirement | Implementation | Verification |
| --- | --- | --- |
| Pinned virtual CPU/platform | `qemu/base-version.txt`, fetch/build scripts, board patch | source-only Linux build; platform configuration test |
| Real firmware binary boot | startup, linker script, platform I/O | `boot.elf`: timer, UART, guest exit |
| Device registers/reset | `model/virtual_dma.c`, shared headers | native `registers` test; `detect.elf` |
| Asynchronous RAM copy | virtual-clock timer, guest memory APIs | native `copy`, `boundary`; polling and interrupt firmware |
| Invalid descriptors | length/range/overlap validation | native `errors` and unchanged-memory assertions |
| Repeated/invalid commands | active snapshot and state checks | native `commands`; driver duplicate-submit test |
| CPU task while DMA pending | nonblocking submit | polling and interrupt firmware CPU-progress assertions |
| Real interrupt delivery | board PLIC wiring, traps, ISR | native `irq`; interrupt firmware result/count checks |
| Enable pending IRQ | device IRQ enable logic | native `irq`; lifecycle firmware with IRQ initially disabled |
| Abort/reset and reuse | timer cancellation and driver lifecycle APIs | native `cancel`; lifecycle firmware old/new buffers and IRQ counts |
| Cancellation/completion race | driver checks the post-ABORT state | `cancel_race.elf`: 128 timed boundary cases, result preservation and exact IRQ count |
| Stall/lost IRQ and recovery | one-shot host faults, timed driver wait/reset | native fault tests; stall/dropirq firmware and fresh transfer |
| DTB/platform consistency | generated DMA node and board constraints | `check-platform.py`, DMA-disabled DTB and rejected configurations |
| Wait API and timer wrap | explicit result codes, coherent snapshot and timeout cleanup | `poll_edges.elf`, `irq_edges.elf`, masked PLIC/reset and repeated wait cases |
| Complete state/command matrix | device state machine | 64 command cases, deadline cancel, system reset, combined faults |
| Useful failure evidence | per-scenario logs/traces and JSON summary | runner error/watchdog tests; CI artifact upload |
| Reviewer demo | manifest-selected firmware scenarios | short demo command run after full suite in CI |
| Reproducible local/CI use | Docker wrapper, pinned base/compiler/QEMU, Makefile | clean arm64 build and hosted Linux CI |

All tests require successful process exit and expected output evidence. Guest copy checks verify actual memory content, source preservation, and guard bytes before reporting success. Device tests directly assert clock, state, memory, and IRQ behavior.

No CPU implementation, additional peripheral, RTL, FPGA, OS driver, or real-chip performance claim is required by the agreed scope. The complete project is the functioning virtual platform, firmware driver, failure validation, build/run workflow, and evidence described above.
