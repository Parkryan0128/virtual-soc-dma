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

The full device suite contains **15 native QTest cases**, with parameterized transfer sizes and invalid-request cases inside them. The guest suite now boots **10 firmware images**: boot, detect, polling, interrupt, lifecycle, cancel_race, stall, dropirq, poll_edges, irq_edges. Twenty-six host tests cover runner/process failures, TAP/manifest validation, interrupted dependency fetches, failed firmware rebuilds, and isolation of evidence between full/partial/demo runs. The short demo selects six of the firmware scenarios. The separate platform check validates board configuration and DTB content.

The expanded audit adds 64 state/command combinations, exact deadline cancellation, system reset, RAM adjacency and end boundaries, delay bounds, combined one-shot faults, and guest timer-wrap/API edges. See [audit.md](audit.md) for fixes and regression evidence.

## Previous baseline

The earlier implementation (`c811f1c1240960d53f9a6d48d05386bc2834a6fa`) passed its 11-group suite and six-scenario demo in clean arm64 and hosted x86-64 environments. [Earlier CI run](https://github.com/Parkryan0128/virtual-soc-dma/actions/runs/37174177012). The expanded audit results below supersede that coverage.

## Evidence locations

Run `./scripts/test-in-container.sh`. The wrapper copies the fresh environment's build log, per-scenario logs, DMA traces, DTBs, and structured `summary.json` to local `artifacts/`. CI uploads that directory even when a test fails. Generated logs/binaries stay out of Git; this document records the observed results.

`summary.json` includes the exact executed command, exit code, and pass/fail outcome for each top-level test. Each guest checks memory/results before emitting its final marker; the host requires both that marker and a successful guest exit. Host watchdog expiry is always a failure.

## Expanded audit verification

Source commit `5aae7887e28a267c484ab84129416f33387827a4` passed a source-only fresh Linux arm64 container build and independently passed hosted Linux x86-64 CI: **13/13 groups** (19 host tests, platform validation, 15 QTests, and 10 firmware scenarios), plus **6/6 demo scenarios**. [Successful audit CI run](https://github.com/Parkryan0128/virtual-soc-dma/actions/runs/37184020506).

Negative regression checks reproduced the previous idle-polling and runner failures. Temporarily removing polling timeout RESET from a separate driver copy caused `FAIL polling timeout resets stalled device` and guest exit 1; the production source passed.

An additional Linux arm64 build of the pinned QEMU with `--enable-asan --enable-ubsan` passed **13/13 groups** using `UBSAN_OPTIONS=halt_on_error=1`. All 15 native device tests and all 10 actual RV32 firmware scenarios passed; retained logs contained no AddressSanitizer, LeakSanitizer, or undefined-behavior error diagnostics. QEMU printed ASan's warning about limited `makecontext`/`swapcontext` support; this run does not imply sanitizer coverage of every coroutine operation.

Instrumented TCG exceeded the normal 30-second watchdog on large-copy firmware. The dedicated sanitizer run used a bounded 180-second host watchdog per group and otherwise kept the same scenario commands and virtual-time settings. Normal local/CI validation retains its 30-second guest watchdog. Local sanitized logs and summary are retained in `artifacts/sanitizer/`; the normal evidence remains in `artifacts/`.

The follow-up documentation commit records these observations only; executable source is unchanged from the CI-tested commit.

## Second full scan (2026-10-04)

The model, board patch, assembly startup/traps, driver, firmware scenarios, build scripts, host runner, native tests and documentation were reviewed again. No additional device or driver behavioral defect was found within the current contract. Five newly added regression checks failed before fixes: partial compile failure, invalid build manifest, removed-scenario ELF retention, stale passing run summary, and overwritten full-suite logs. The expanded 26-test host suite passes after fixes, including interruption and partial-run isolation cases.

A Linux arm64 QEMU build with `--extra-cflags=--coverage --extra-ldflags=--coverage` passed the full 13-group suite, six-scenario demo and detect-stage entry point. GCC gcov reported **137/141 executable lines (97.16%)** and **72/77 branch outcomes taken (93.51%)** for `model/virtual_dma.c`. The unexecuted lines are the QOM finalizer: this board-owned, non-hotpluggable device is not explicitly destroyed by the suite. Untaken branches include internal invariant/memory-transaction failures and an unreachable command-switch default. These are model-only coverage figures, not firmware or whole-QEMU coverage, and do not prove exhaustive correctness.

GCC `-fanalyzer` completed without diagnostics for `model/virtual_dma.c`, `firmware/drivers/dma.c`, and `firmware/drivers/platform_io.c`. The model and firmware source are unchanged from the preceding successful ASan/UBSan audit. Current changes concern build publication and test evidence handling.

Final source-only rebuild and hosted CI are being checked before recording their source revision and results. Local raw evidence is retained under `artifacts/rescan-*` and `artifacts/coverage/`.
