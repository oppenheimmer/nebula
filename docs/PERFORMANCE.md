# Performance verification

Measurements use the real editor/window, local SQLite and managed files. Run on
an otherwise idle machine; record the image, compiler, Qt, hardware and backend.
They measure synchronous event processing, rather than compositor latency or OS
input-method performance. Linux peak RSS comes from `/proc/self/status`.

```sh
./scripts/dev.sh benchmark single
./scripts/dev.sh benchmark workspace
```

The M01 reference fixture is 1,000 paragraphs. Its existing investigation budgets
are 500 ms load, 50 ms typing p95, 100 ms adjacent move and 128 MiB peak RSS.
See [EDITOR_FEASIBILITY](EDITOR_FEASIBILITY.md) for the original comparison.

For M08, the integrated fixture contains a 10,000-block page, 5,000 dated Journal
entries, a hub with 1,200 referrers and 200 managed image cards. The following
budgets were selected before the continuation's measurements, using the proposed
budgets from the Suggestions checkpoint. They guide this reference-machine
release assessment; they are not portable CI timing assertions or guarantees for
every user's hardware.

| Measurement | Budget |
| --- | --- |
| Typing p95, each measured page | 16 ms |
| Save after body edits, each measured page | 250 ms |
| Hub rename and default-label propagation | 1,000 ms |
| Open a measured page | 1,000 ms |
| Switch to 5,000-entry Journal | 300 ms |
| Peak RSS | 128 MiB |

Run three consecutive measurements after builds/tests finish. Record all values
and investigate any budget exceedance; do not quietly discard an outlier. The
10,000-block move, search and image-import measurements remain diagnostic without
additional timing gates. Fixtures are temporary and use no user workspace.

Concurrent maintenance is checked separately by `reliability_tests`: the full
window types every 650 ms while a separate read-only process backs up and exports JSON/Markdown from
a workspace containing 2,001 pages and 32 MiB of body text. It requires at least
three actual autosaves, no visible save failure, and valid restored/imported
copies. Its large temporary fixture uses the ignored build directory to avoid
the container's bounded `/tmp` tmpfs. This checks process interruption and locking,
not hardware power-loss durability.

Actual results, deviations and remaining release issues belong in
[ROADMAP](../ROADMAP.md).
