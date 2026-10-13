# virtual-soc-dma

A DMA device for QEMU and a bare-metal RISC-V driver. The firmware copies memory through MMIO, handles PLIC interrupts, and recovers from stalled transfers and missing interrupts.

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

## Device

The platform is fixed: QEMU `virt`, one RV32 hart, M-mode, 128 MiB RAM and PLIC (`aia=none`). Enable the device with `-M virt,dma=on,aia=none`; stock QEMU does not include it.

- RAM: `0x80000000`–`0x87ffffff`.
- DMA registers: `0x10010000`–`0x10010fff`, aligned 32-bit little-endian access.
- PLIC interrupt: 16.
- One channel, RAM-to-RAM copies of 1–65,536 bytes. Byte-aligned buffers are allowed; overlapping buffers are rejected.

Addresses and register constants are shared in [platform/](platform/).

| Offset | Register | Meaning |
| --- | --- | --- |
| `0x00` | ID | `0x56444d41` (read-only) |
| `0x04` | VERSION | `0x00010000` (read-only) |
| `0x08` | SRC | Source address |
| `0x0c` | DST | Destination address |
| `0x10` | LEN | Byte count |
| `0x14` | COMMAND | START=1, ABORT=2, ACK=4, RESET=8; reads as zero |
| `0x18` | STATUS | IDLE=0, BUSY=1, DONE=2, ERROR=4 (read-only) |
| `0x1c` | ERROR_CODE | Length=1, source=2, destination=3, overlap=4 (read-only) |
| `0x20` | IRQ_ENABLE | Bit 0 enables the interrupt |
| `0x24` | IRQ_PENDING | Sticky completion/error flag (read-only) |

START accepts work only in IDLE and snapshots SRC/DST/LEN. Validation checks length, source range, destination range, then overlap. Invalid requests report an error without copying. A valid transfer reads the source and copies all bytes at completion, so leave both buffers alone while BUSY.

Write one command bit at a time. START and SRC/DST/LEN writes are ignored while BUSY. ACK clears DONE/ERROR and the pending flag; ABORT cancels BUSY work; RESET cancels work and clears writable registers, including IRQ_ENABLE. ACK and ABORT preserve the transfer configuration. Reserved offsets/bits and writes to read-only registers are ignored. Unsupported access widths or alignment do not change device state.

Completion and errors set IRQ_PENDING; the IRQ line follows `IRQ_ENABLE && IRQ_PENDING`. The ISR records the result, ACKs the DMA, then completes the PLIC claim. Enabling IRQ after completion delivers the pending event.

## Driver and tests

The [driver](firmware/drivers/dma.h) has polling and interrupt APIs. Use one mode per request, from foreground code on the single hart. IRQ mode allows one outstanding request; consume or reset its result before submitting again.

Wait results are `0` for a terminal result (check DONE/ERROR), `-1` for timeout, `-2` for no request and `-3` for a null output pointer. Invalid/no-request calls leave outputs and device state unchanged. Timeouts preserve diagnostics and reset the device; IRQ reset also drains old PLIC events and re-enables device IRQs.

The 10 ms deadline starts at submission for IRQ mode and at wait for polling. IRQ mode requires timely ISR delivery: a result read later still succeeds, but a late or lost IRQ times out even if the copy finished. Cancel succeeds only while BUSY; if completion wins the race, its result remains available to wait.

QTest checks registers, memory, timing and IRQ wiring. Guest firmware tests polling, interrupts, cancellation and recovery. Host tests cover the build scripts and test runner. Test faults are one-shot:

```text
-global virtual-dma.stall-next=on       # next valid transfer stays BUSY
-global virtual-dma.drop-irq-next=on    # next DONE/ERROR has no external IRQ
-global virtual-dma.delay-ns=1000000    # completion delay: 1–1,000,000,000 ns
```

For debugging, add `-S -gdb tcp::1234` to a recorded QEMU command and run it directly, outside the test runner's watchdog.

Licensing for the firmware and QEMU code is listed in [LICENSE](LICENSE).
