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

The full device suite contains **10 native QTest cases**, with parameterized transfer sizes and invalid-request cases inside them. The guest suite now boots **8 firmware images**: boot, detect, polling, interrupt, lifecycle, cancel_race, stall, dropirq. Four host-runner tests verify failed exits, missing evidence, missing binaries, and watchdog expiration. The short demo selects six of the firmware scenarios. The separate platform check validates board configuration and DTB content.

## Clean-build and CI status

A source-only fresh-container run completed successfully with exit status 0. Its source archive excluded `build/`, `artifacts/`, and `.git`: pinned QEMU and all firmware were built again from source. The previous clean build passed its original six firmware scenarios. The expanded source-only check adds lifecycle firmware, runner failure tests, and the short demo; its final result is recorded below when verified.

The hosted GitHub Actions Linux validation completed successfully for implementation commit `faa2afeeb0a2d47c1e727e824dd914395507a94f`. It independently built the source-only Linux container and ran the complete device, platform, and firmware suite. [Successful CI run](https://github.com/Parkryan0128/virtual-soc-dma/actions/runs/37171162478).

The follow-up completion audit adds foreground driver cancellation/reset, lifecycle firmware, a scenario manifest, runner failure tests, and a dedicated short demo. The final expanded code is undergoing fresh-container and hosted-CI validation; the earlier linked run validates the earlier implementation, not these additional changes.

## Evidence locations

Run `./scripts/test-in-container.sh`. The wrapper copies the fresh environment's build log, per-scenario logs, DMA traces, DTBs, and structured `summary.json` to local `artifacts/`. CI uploads that directory even when a test fails. Generated logs/binaries stay out of Git; this document records the observed results.

`summary.json` includes the exact executed command, exit code, and pass/fail outcome for each top-level test. Each guest checks memory/results before emitting its final marker; the host requires both that marker and a successful guest exit. Host watchdog expiry is always a failure.
