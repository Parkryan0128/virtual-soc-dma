# Edge-case audit

The original suite covered normal copies, invalid descriptors, interrupts, cancellation and fault recovery. Reviewing the implementation against the contract uncovered additional API and test-infrastructure gaps. Passing the earlier suite did not imply those cases were covered.

## Fixes and regression evidence

| Finding | Change | Regression |
| --- | --- | --- |
| Polling an idle device returned success with STATUS=0 | Return NO_REQUEST without changing outputs | `poll_edges`: failed on the previous driver, passes with the fix |
| Polling timeout left the device BUSY and diagnostics unset | Snapshot state, reset/cancel the transfer, then return TIMEOUT | Stalled polling request, unchanged destination, fresh successful copy; removing timeout RESET makes this test fail |
| Null wait output could fault or corrupt guest memory | Reject missing outputs without consuming the request | Both polling output pointers and IRQ result pointer |
| Timeout reads could mix BUSY with a later completion's pending flag | Shared state snapshot rechecks STATUS | State-transition review; complete timeout diagnostics in guest tests |
| Substring matching accepted `NOT PASS ...` or output containing both FAIL and PASS | Require complete marker lines and reject explicit FAIL | New host regression cases failed before the fix |
| Failed launch kept a previous successful stdout/trace | Replace per-run evidence before starting; clear clean-build summary before a new build | Successful fixture followed by missing executable |
| Invalid UTF-8 aborted the runner without a structured result | Retain raw bytes and record a failed result | Raw invalid-byte output fixture |
| Watchdog killed the parent but left child processes alive | Launch an isolated process group and terminate the whole group | Child scheduled to write a file after the watchdog; previous runner allowed it |
| Empty scenario selection could report success; build list and run list were duplicated | Validate a shared manifest used by compiler and runner | Empty/duplicate/unsafe names, missing markers, bad fields, empty selection |
| Native test check only looked for fixed marker fragments | Validate the full nonempty TAP plan, numbering, unique names and outcomes | Missing, duplicate, failed, skipped and bailed-out test output |
| Python optimization removed platform assertions | Use explicit validation failures; route legacy boot command through the shared runner | Platform checks executed with `python3 -O`; boot/detect entry points exercised |
| Interrupted initial dependency fetch left `.git` without HEAD and could not recover | Retry fetch only when no revision exists; reject an existing wrong revision | Real temporary local Git remote, no network or destructive reset |

## Device and firmware coverage

- Native QTest: 15 groups, including all 16 command bit patterns in IDLE/BUSY/DONE/ERROR (64 combinations), ignored reserved bits, read-only/access-width rules, and exact state/IRQ/configuration preservation.
- Memory: lengths 1, 3, 1024 and 65,536; source/destination content and guard checks; first/last RAM bytes; transfers ending exactly at RAM end; adjacent buffers in both directions; zero/oversized length; invalid, overflowing and overlapping ranges; error precedence; no writes on rejection.
- Timing/lifecycle: completion visibility, cancellation 1 ns before and at completion, sequential reuse, QEMU system reset from BUSY/DONE/ERROR, minimum/maximum valid delay and rejection outside those bounds.
- Faults: stall and lost IRQ individually and together; a cancelled stalled transfer does not consume the next-terminal-event lost-IRQ fault; recovery has no stale writes or interrupts.
- Real RV32 firmware: 10 scenarios. Added empty/repeated wait, null outputs, timeout cleanup, timer wrap, reset with an unclaimed PLIC completion while CPU interrupts are masked, reset after ISR delivery but before result consumption, repeated reset/init and subsequent reuse.
- Host: 26 tests covering runner/process failures, manifest selection and dependency-fetch recovery. The six-scenario reviewer demo remains concise.

## Validation record

The expanded suite passed normal incremental execution, a source-only fresh Linux arm64 container, hosted Linux x86-64 CI, and an ASan/UBSan Linux arm64 build. The six-scenario demo also passed in the clean build and CI. Exact source revision, run links, sanitizer settings and limits are recorded in [validation.md](validation.md).

The audit does not claim exhaustive state-space exploration or real-chip timing coverage. The model intentionally has a single hart/channel, atomic completion, no cache/coherency/IOMMU, and no RTL. The tested boundaries above match that contract.

## Second scan findings (2026-10-04)

The second scan found additional issues in incremental build and evidence handling. A failed firmware compilation could leave old or partly updated ELFs runnable, and removing a scenario did not remove its old ELF. The builder now invalidates the old set and publishes a complete staged set only after all compiles succeed. Regression tests exercise partial compiler failure, invalid manifest and successful replacement of obsolete images.

A demo or partial run could overwrite full-suite UART/model evidence, while setup failure or interruption could leave an earlier passing summary. Each run mode now owns its own evidence directory; summaries start in an incomplete state and are atomically replaced with final results. Reserved manifest names cannot collide with host-test/evidence directories. Real subprocess tests verify isolation and failed/interrupted-run summary behavior.

The expanded host suite has 26 tests. The full coverage-instrumented suite and GCC static analysis passed; model coverage gaps were inspected explicitly. See [validation.md](validation.md) for exact measurements and final clean-build/CI status.
