# virtual-soc-dma

A virtual DMA device and bare-metal RISC-V driver for testing firmware before hardware exists.

The firmware runs as a cross-compiled RV32 ELF inside QEMU. It programs the DMA through MMIO, receives a PLIC interrupt, and checks the copied memory. Host-side fault options simulate a stalled transfer or a missing interrupt so the driver can exercise timeouts and recovery.

```text
RV32 firmware → MMIO registers → DMA model → guest RAM
      ↑                             │
      └──── machine interrupt ← PLIC┘
```

## Scope

- QEMU `virt`, one RV32 hart, M-mode bare metal, 128 MiB RAM, no OS.
- One DMA channel for contiguous RAM-to-RAM copies of 1–65,536 bytes.
- Polling and interrupt APIs, validation errors, abort/reset, bounded waits and reuse after failure.
- QTest device tests and end-to-end firmware scenarios, built and run in Linux containers.

QEMU supplies the CPU, RAM, UART, timer and PLIC. This repository supplies the DMA model, board integration, driver and tests. The pinned dependency is QEMU `v10.0.0`, commit `7c949c53e936aa3a658d84ab53bae5cadaa5d59c`; a stock QEMU binary does not contain the device.

The model copies all bytes at one completion event. Its configurable virtual-time delay demonstrates asynchronous operation; it does not model bandwidth, bus contention or real-chip performance. RTL, FPGA, Linux drivers, cache coherency, IOMMU, scatter-gather and multiple channels are outside this project's scope.

## Implementation checkpoints

| Checkpoint | Implemented behavior | Verification |
| --- | --- | --- |
| Boot and integration | RV32 startup, UART/timer, opt-in DMA and DTB node | Boot/detection firmware, register and platform tests |
| Transfers | Asynchronous copy, range/length/overlap checks, polling | QTest memory/boundary cases and polling firmware |
| Interrupts | PLIC routing, trap/ISR handling, nonblocking submission | Interrupt firmware, pending-IRQ delivery and deadline cases |
| Recovery | Abort/reset, cancellation races, stall/lost-IRQ timeouts, fresh transfers | QTest lifecycle/fault cases and recovery firmware |
| Build and validation | Pinned QEMU, firmware builds, full suite and short demo | Clean container run and GitHub Actions |

This is the complete single-channel bare-metal scope. Adding an OS driver or RTL would be a separate extension.

## Layout

```text
model/              QEMU DMA device and trace events
platform/           Shared addresses and register definitions
firmware/           Startup/traps, driver and guest test programs
qemu/               Pinned revision and board/build integration patch
scripts/            Build scripts, test runner and platform checks
tests/              QTest cases, scenario manifest and host regression tests
docs/device-spec.md Device and driver contract
```

The [device specification](docs/device-spec.md) defines the registers, state transitions, fault controls and driver return values.

## Build and run

With Docker running, from the repository root:

```sh
make test   # clean build, full validation suite, then the short demo
make demo   # clean build and six demonstration scenarios
```

The wrapper builds the Linux toolchain image, copies project sources into a disposable container, fetches pinned QEMU, and cross-compiles the firmware. It requires network access on the first run and takes several minutes. It does not need a host RISC-V compiler or a host bind mount.

For incremental work inside a Linux environment with the dependencies from `Dockerfile`:

```sh
make build
python3 scripts/run-tests.py
python3 scripts/run-tests.py --scenario lifecycle
./scripts/run-demo.sh
```

A failed firmware or QEMU build invalidates its executable outputs, preventing tests from running old or partially updated binaries. `--stage boot`, `detect`, `polling` and `irq` select progressively broader checks; run the full suite before publishing changes.

The demo covers boot, detection, interrupt copies, abort/reset, a stalled transfer and a dropped IRQ. In the stall case memory remains unchanged. In the dropped-IRQ case the copy finishes but the driver's interrupt wait times out. Both reset the device and complete a fresh transfer.

Host fault options used by the test runner:

```text
-M virt,dma=on,aia=none -cpu rv32 -smp 1 -m 128M
-global virtual-dma.delay-ns=1000000
-global virtual-dma.stall-next=on
-global virtual-dma.drop-irq-next=on
```

The patched board creates the device; it is not a standalone `-device` option. Unsupported DMA platform configurations are rejected, and VM migration is disabled for this device.

## Test output and debugging

A full run writes `artifacts/summary.json` plus each group's UART output, QEMU diagnostics and DMA traces. The demo uses `artifacts/demo/`, stage runs use `artifacts/stages/<stage>/`, and individual scenarios use `artifacts/scenarios/`. Demo builds also keep their build log under `artifacts/demo/`, preserving full-suite results.

Each result records the command, exit code and artifact directory in the execution environment. The runner requires successful exit and complete expected output; guest programs check actual data, source preservation and guard bytes before reporting success. A host watchdog treats hangs as failures. GitHub Actions runs the same clean-container suite and uploads `artifacts/`.

For interactive debugging, use a scenario's recorded QEMU command with `-S -gdb tcp::1234` and attach a RISC-V-capable GDB using the matching ELF. Run QEMU directly so the test runner's watchdog does not end the debugging session.

## License and references

Firmware, scripts, shared register headers and documentation use MIT. QEMU device code, native QTest code and integration patches use GPL-2.0-or-later. See [LICENSE](LICENSE) and the [GPL text](LICENSES/GPL-2.0.txt).

- [QEMU RISC-V virt platform](https://www.qemu.org/docs/master/system/riscv/virt.html)
- [QTest framework](https://www.qemu.org/docs/master/devel/testing/qtest.html)
- [Pinned QEMU board source](https://github.com/qemu/qemu/blob/v10.0.0/hw/riscv/virt.c)
