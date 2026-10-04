# Implementation plan

## Progress

All milestones 0–5 and their acceptance gates are complete. The final expanded source passed the full suite and short demo in a source-only fresh Linux arm64 container and hosted Linux x86-64 GitHub Actions. Actual environment/results are recorded in [validation.md](validation.md). The sections below preserve the gate definitions used for implementation.

## Objective and boundary

Deliver one executable demonstration of pre-silicon software validation: boot a RISC-V firmware binary on a virtual platform, control a custom DMA device through MMIO, handle interrupts, and recover from injected failures.

This plan implements the [device contract](device-spec.md). Complete milestones in order. Each acceptance gate requires observed results, not merely code or sample output. Do not add extra peripherals or performance claims while an acceptance gate is incomplete.

## Planned repository layout

```text
README.md
docs/
  device-spec.md
  implementation-plan.md
  demo.md                    # added after the demo works
platform/
  platform.h                 # fixed board addresses/IRQ; shared with integration
  dma_regs.h                 # model/driver register contract
model/
  virtual_dma.c              # native QEMU C device
  virtual_dma.h
  trace-events
qemu/
  base-version.txt           # exact upstream tag + resolved SHA
  patches/                   # board wiring, build registration, QTest integration
firmware/
  startup.S
  traps.S
  linker.ld
  drivers/                   # UART, timer, PLIC, DMA
  tests/                     # selectable guest test cases
tests/
  qtest/                     # native libqtest C tests
  scenarios/                 # host test matrix/configurations
scripts/
  fetch-qemu.sh
  build-qemu.sh
  build-firmware.sh
  run-tests.py
  run-demo.sh
.github/workflows/ci.yml
```

Generated downloads, QEMU checkout, host binaries, and firmware objects belong in ignored `build/`. Test evidence belongs in ignored `artifacts/` and CI uploads. Add directories as their implementation lands, not as empty placeholders.

## Milestone 0 — Reproducible firmware boot

1. Resolve QEMU `v10.0.0` to an exact commit and record it. Inspect board addresses, free PLIC source, QOM properties, reset/timer interfaces, and DMA memory APIs in that revision.
2. Freeze one RV32 CPU configuration, 128 MiB RAM, one hart, TCG, PLIC, and M-mode boot with no OpenSBI. Confirm ISA/compiler flags and ELF loading/start address experimentally.
3. Build only `riscv32-softmmu`. Use an RV32-capable bare-metal compiler (for example a verified `riscv64-unknown-elf-gcc` multilib toolchain targeting RV32); record the tested toolchain version and required multilib.
4. Add startup, stack, BSS initialization, linker script, UART output, timer reads, and an explicit guest success/failure exit mechanism.
5. Boot a minimal firmware image on stock pinned `virt` and print a boot marker. Add a host watchdog and check exit status.

**Gate:** a clean supported Linux environment can repeat the boot from documented scripts. Confirm the proposed DMA MMIO/IRQ allocation is conflict-free. If a baseline/toolchain change is necessary, update the docs and pin before device implementation.

Linux is the initial required build/CI environment. Document macOS prerequisites or a verified Linux VM/container route after bring-up; do not claim native macOS support before testing it.

## Milestone 1 — Device skeleton and board integration

1. Implement a QOM SysBus device with one 4 KiB MMIO region, one IRQ output, register fields, and reset behavior.
2. Add a minimal opt-in patch to `virt`: instantiate the device, supply its allowed RAM window, map registers, route the selected PLIC source, and add the DTB node. Keep ordinary `virt` unchanged by default.
3. Register the device/build configuration and QTest source through the patch set. Build the modified QEMU from a fresh checkout.
4. Add QTests for ID, reset values, reserved/RO access, access width, and IRQ enable/pending rules available at this stage.
5. Firmware reads the device ID and prints a detection marker.

**Gate:** the patched board boots the same firmware, the device is present at the documented address, reset/access tests pass, and the patch applies cleanly to the pinned revision.

## Milestone 2 — Asynchronous DMA and polling path

1. Implement accepted START snapshots and complete validation before memory access.
2. Schedule completion on QEMU virtual time; copy through guest memory APIs; set terminal status. No host sleep, wall-clock completion timer, or background thread.
3. Implement ACK, ABORT, RESET, and cancellation that prevents stale completions.
4. Add trace events for accepted/rejected command, snapshot, scheduled deadline, completion/error, cancellation, and IRQ level.
5. Implement the firmware DMA polling driver with timer deadlines and memory-ordering barriers appropriate to the chosen RISC-V MMIO interface.
6. Run QTest boundary/error/cancellation cases and polling firmware data/guard-byte tests.

**Gate:** all transfer sizes and validation cases in the spec pass. Advancing time past a cancelled deadline produces no late write or IRQ. A CPU counter/task progresses while DMA is pending. No real-device performance inference is made.

## Milestone 3 — Interrupt-driven driver

1. Add M-mode trap entry/return, external interrupt routing, PLIC priority/enable/threshold, and claim/complete handling.
2. Add a nonblocking DMA submit path and an ISR-written completion/result flag. The foreground waits against a timer deadline; it does not block indefinitely in WFI.
3. Snapshot DMA terminal result before ACK, lower the device IRQ, then complete the PLIC claim. Protect shared ISR/foreground state with correct volatile/compiler/architecture ordering; document the single-hart assumptions.
4. Test disabled IRQ, enable-after-pending, repeated transfers, correct data after notification, and absence of an interrupt storm.

**Gate:** firmware receives completion/error through the real emulated CPU interrupt path and can run sequential transfers without stale completion flags or IRQs. Polling tests still pass.

## Milestone 4 — Failure and recovery

1. Implement host-configured one-shot stall and dropped-IRQ modes exactly as specified.
2. Add QTests proving the distinct outcomes: stall remains BUSY; dropped IRQ has terminal status/pending state but no external IRQ.
3. Add driver timeout diagnostics including STATUS, ERROR_CODE, and IRQ_PENDING. Reset/reinitialize after timeout and perform a fresh transfer.
4. Test reset/abort during pending work, re-enabling IRQ after reset, and the absence of old callbacks corrupting a new operation.
5. Use controlled virtual time for QTest and a fixed instruction-count/virtual-time configuration for firmware runs where supported by the pinned QEMU. Confirm guest deadlines and host watchdog behavior empirically.

**Gate:** both fault scenarios fail in the expected bounded way, recover, and complete a fresh transfer correctly. Logs distinguish DMA stall, lost IRQ, and guest validation error.

## Milestone 5 — Clean build, CI, and demo

1. Provide documented scripts to fetch the exact QEMU commit, apply patches, build both binaries, and run the full suite.
2. Test the full dependency path from a clean Linux environment. Pin the tested compiler/dependency setup; validate downloaded revision/checksums as appropriate. Reuse Docker dependency-image layers; the clean verification path deliberately rebuilds QEMU and firmware. Incremental Linux scripts reuse the pinned checkout/build directory.
3. Add Linux CI for QTest plus firmware scenarios; upload UART/model traces and a structured summary on failure.
4. Add a short text demo: boot, device detection, interrupt copy, stall timeout/recovery, dropped-IRQ timeout/recovery, final suite result. Generate actual output before documenting it.
5. Document commands, architecture, limitations, debugging with traces/GDB, and attribution/license requirements for QEMU-derived code. Preserve upstream notices in integration patches and choose compatible licenses for added device code before code publication.

**Gate:** README commands work, clean build succeeds, all required tests pass in CI, and a reviewer can reproduce the demo without hardware or private tools.

## Evidence and test responsibilities

| Layer | Evidence |
| --- | --- |
| Device model | QTest assertions for register/state/IRQ/memory behavior with virtual-clock stepping |
| Firmware integration | Cross-compiled ELF boots, real trap/ISR path, exact copy and guard-byte checks |
| Recovery | Expected timeout diagnostics plus successful fresh transfer after reset |
| Reproducibility | Pinned source/toolchain, clean build, CI exit status and retained traces |

Each firmware scenario runs in a fresh QEMU process except repeated-transfer and recovery cases, which must retain state to prove reuse. UART markers alone are insufficient: success requires content checks, expected scenario progression, and the guest/host exit result. A watchdog expiration is always a failure, never an expected timeout success.

## Main risks and controls

- **QEMU integration overhead:** prove boot first; use one pinned revision and a small patch set; avoid a separate CPU emulator or remote co-simulation bridge.
- **Interrupt bugs:** retain polling as a diagnostic path; compare device-line tests with actual firmware trap tests.
- **False recovery:** assert no late writes and use distinct buffers/patterns for the post-reset transfer.
- **Misleading timing:** report functional correctness and scheduling behavior only; no bandwidth/speedup benchmarks.
- **Scope growth:** finish the one-channel bare-metal scope before considering OS drivers, SystemC, or RTL.

## Definition of done

All README completion criteria and all specification test rows have evidence, CI passes on the pinned environment, the demo is reproducible, and documentation describes implemented behavior. Until then, label incomplete milestones plainly and do not present intended tests as passing results.
