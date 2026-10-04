# Demo

This demo runs actual RISC-V firmware inside patched QEMU. It validates the device/driver interface and recovery behavior; it does not measure real DMA bandwidth.

## Run

After building inside the Linux development environment:

```sh
./scripts/run-demo.sh
```

For the short demo directly from macOS or Linux with Docker:

```sh
make demo
```

For a clean build, all tests, and the demo:

```sh
./scripts/test-in-container.sh
```

## Observed firmware output

Selected lines from the locally executed scenarios:

```text
BOOT RV32 M-mode
PASS timer
PASS boot
BOOT RV32 M-mode
PASS DMA detection
BOOT interrupt
PASS interrupt copy 0x00000001
PASS interrupt copy 0x00000003
PASS interrupt copy 0x00000400
PASS interrupt copy 0x00010000
PASS interrupt suite
BOOT lifecycle
PASS firmware abort and reuse
PASS firmware reset and reuse
PASS firmware enable pending interrupt
PASS lifecycle suite
BOOT stall
TIMEOUT status=0x00000001 error=0x00000000 pending=0x00000000
PASS stall timeout
PASS stall recovery
BOOT dropirq
TIMEOUT status=0x00000002 error=0x00000000 pending=0x00000001
PASS dropirq timeout
PASS dropirq recovery
PASS demo: 6/6 groups
```

In the stall run the destination stays untouched until the driver's deadline expires. In the dropped-IRQ run the data is already copied, but the ISR never reports completion. Both recover by resetting/reinitializing the device and executing a new transfer into a different buffer. The model's fault options are one-shot, so that second transfer executes normally.

The interrupt test includes a small CPU task while DMA is BUSY. This demonstrates nonblocking operation only: the model's configurable completion delay is not a real hardware latency measurement.

Inspect `artifacts/interrupt/model.trace`, `artifacts/stall/model.trace`, and `artifacts/dropirq/model.trace` to follow START, completion scheduling, IRQ transitions, faults, and resets alongside the UART logs.
