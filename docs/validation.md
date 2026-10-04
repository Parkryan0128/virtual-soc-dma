# Validation evidence

## Environment

Local validation runs on Linux aarch64 inside Docker on an Apple Silicon Mac. The Docker base is Ubuntu 24.04, pinned by the multi-platform manifest digest in `Dockerfile`.

| Dependency | Observed version |
| --- | --- |
| QEMU | v10.0.0 / `7c949c53e936aa3a658d84ab53bae5cadaa5d59c` |
| RISC-V bare-metal GCC | 13.2.0 (`13.2.0-11ubuntu1+12`) |
| Host GCC | 13.3.0 |
| Python | 3.12.3 |
| QEMU build Meson | 1.5.0, bootstrapped by pinned QEMU |
| Ninja | 1.11.1 |
| GLib development package | 2.80.0-6ubuntu3.9 |
| libfdt development package | 1.7.0-2build1 |

Firmware targets `rv32im_zicsr` / `ilp32`, with no libc or OS. Model and native QTests use the QEMU host toolchain. Ubuntu package repositories may advance; the base and QEMU revision are pinned and these are the observed dependency versions, not a fully immutable package snapshot.

## Incremental gates observed

1. Stock pinned QEMU build: boot ELF, timer progression, UART markers, and normal guest exit passed.
2. Device integration: reset values, ID/version, RO/reserved accesses, invalid access sizes/alignment, firmware detection passed.
3. Asynchronous copy: sizes 1/3/1024/65,536, byte alignment, data/guard/source checks, error precedence, address overflow, exact RAM boundaries, command rejection, level IRQ, cancel-and-reuse passed.
4. Real firmware interrupts: trap entry/return, PLIC claim/complete, repeated transfers, immediate error notification, CPU task progress, and no interrupt storm passed.
5. Fault recovery: stall remained BUSY; dropped IRQ left DONE plus IRQ_PENDING. Both guest deadlines expired, reset cleared state, and one fresh transfer completed through the ISR. Distinct old/new buffers and a later observation interval checked for stale writes/interrupts.
6. Expanded lifecycle firmware: explicit abort/reset and new transfer, pending IRQ enable, and rejection of cancellation after completion passed in the incremental Linux environment.
7. Cancellation/completion race: 128 actual guest transfers with a 342 ns model delay produced both abort and completion outcomes. Successful aborts left memory untouched; completed requests retained their ISR result. Guard bytes and exact interrupt counts passed. Temporarily removing the post-ABORT state check made this regression fail (`FAIL cancel owns no result`); restoring the fix passed.
8. Platform checks: DMA DTB address/size/IRQ/PLIC parent, absence when disabled, and rejection of multicore/wrong RAM/AIA configurations passed.

The full device suite contains **15 native QTest cases**, with parameterized transfer sizes and invalid-request cases inside them. The guest suite now boots **10 firmware images**: boot, detect, polling, interrupt, lifecycle, cancel_race, stall, dropirq, poll_edges, irq_edges. Nineteen host tests cover runner failures and process cleanup, TAP/manifest validation, and interrupted dependency-fetch recovery. The short demo selects six of the firmware scenarios. The separate platform check validates board configuration and DTB content.

The expanded audit adds 64 state/command combinations, exact deadline cancellation, system reset, RAM adjacency and end boundaries, delay bounds, combined one-shot faults, and guest timer-wrap/API edges. See [audit.md](audit.md) for fixes and regression evidence.

## Previous clean-build and CI baseline

A source-only fresh-container run of source commit `c811f1c1240960d53f9a6d48d05386bc2834a6fa` completed successfully with exit status 0 on Linux arm64. Its source archive excluded `build/`, `artifacts/`, and `.git`: pinned QEMU and all eight firmware images were built again from source. The full suite passed **11/11 groups** (runner, platform, QTest, and eight guest scenarios); the short demo passed **6/6 groups**. The cancellation race exercised 26 successful aborts and 102 preserved completions.

Hosted Linux x86-64 validation completed successfully for this exact source commit. It independently built the development image and source-only fresh container, then passed the full suite and short demo. [Successful final CI run](https://github.com/Parkryan0128/virtual-soc-dma/actions/runs/37174177012).

The final documentation commit only records these observed results and lifecycle semantics; executable source matches the tested commit.

## Evidence locations

Run `./scripts/test-in-container.sh`. The wrapper copies the fresh environment's build log, per-scenario logs, DMA traces, DTBs, and structured `summary.json` to local `artifacts/`. CI uploads that directory even when a test fails. Generated logs/binaries stay out of Git; this document records the observed results.

`summary.json` includes the exact executed command, exit code, and pass/fail outcome for each top-level test. Each guest checks memory/results before emitting its final marker; the host requires both that marker and a successful guest exit. Host watchdog expiry is always a failure.

## Expanded audit verification

The expanded suite passes incrementally. Final source-only and sanitizer builds and hosted CI are being verified before recording their results.
