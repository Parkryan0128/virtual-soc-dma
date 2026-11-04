# virtual-soc-dma

A custom DMA device for QEMU's RISC-V `virt` machine, with a bare-metal C driver. Tests cover memory copies, invalid requests, interrupts, cancellation, and recovery from stalled transfers or lost interrupts.

The model uses a fixed completion delay (1 ms by default). It tests firmware behavior, not hardware bandwidth or bus timing.

## Run

With Docker running:

```sh
make test   # build QEMU and firmware, run all tests and the demo
make demo   # build and run six demo scenarios
```

The first build downloads [pinned QEMU](qemu/base-version.txt) and the Linux toolchain. It takes a few minutes; no host RISC-V compiler is needed.

For incremental work inside the Linux toolchain environment from `Dockerfile`:

```sh
make build
python3 scripts/run-tests.py
python3 scripts/run-tests.py --scenario lifecycle
python3 scripts/run-tests.py --demo
```

Results go to `artifacts/summary.json`, with UART logs, QEMU diagnostics and DMA traces in each test's directory. Demo results live under `artifacts/demo/`. CI runs the same container tests and uploads these files.

## How it works

The driver writes a source address, destination address and byte count to the DMA registers, then issues START. The device copies the bytes after its configured delay; the driver checks the result by polling or handling a PLIC interrupt.

## Device and driver

The device runs on QEMU `virt` with one RV32 hart, M-mode and 128 MiB RAM. It has one channel for RAM-to-RAM copies up to 64 KiB. Overlapping buffers are rejected.

The [driver](firmware/drivers/dma.h) supports polling and interrupts, with one request at a time. See the [device and driver reference](docs/device.md) for registers, command behavior, return values and timeout rules.

## Tests and debugging

QTest checks registers, memory, timing and IRQ wiring. Guest firmware tests polling, interrupts, cancellation and recovery. Host tests cover the build scripts and test runner.

Use these QEMU options to inject a one-shot fault or change the completion delay:

```text
-global virtual-dma.stall-next=on       # next valid transfer stays BUSY
-global virtual-dma.drop-irq-next=on    # next DONE/ERROR has no external IRQ
-global virtual-dma.delay-ns=1000000    # completion delay: 1–1,000,000,000 ns
```

For debugging, add `-S -gdb tcp::1234` to a recorded QEMU command and run it directly, outside the test runner's watchdog.

Licensing for the firmware and QEMU code is listed in [LICENSE](LICENSE).
