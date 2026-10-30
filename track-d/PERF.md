# Track D: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 72.69 | 400 | 0 |
| 1 | Minimal C++20 baseline (fast buffered integer parser, CSR graph, epoch-indexed Dijkstra) | Pending board dispatch | (local full: 9.5s) | 30 (image MB) | Pending board run |

### Verification Evidence & Measurement Log

- **Docker Image Size**: 30 MB (verified with `docker image inspect`, allowlist compliant)
- **Local Timings (containerized with check.py)**:
  - Tiny (40 queries): 0.36 s (SHA-256 `ecaca77d3221fe3928109da36480cfdb39f288f2e57ecbc61d3d7706d5cb4bd5` — PASS)
  - Sample (200 queries): 0.78 s (SHA-256 `a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0` — PASS)
  - Full (2,000 queries): 9.56 s (SHA-256 `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` — PASS)
- **Fresh-seed Generalization (Sample scale)**:
  - Seed 42: 0.92 s (SHA-256 `e003ecd1553a9010cca836975f759aa8588403070e83ed359cc3fd991a2275a6` — PASS)
  - Seed 99: 1.25 s (SHA-256 `7d8a37adcc1fdb2bd8668d47513e42ad8f4459d363553cba96a2cd32994bf9cb` — PASS)
- **Official Liveboard Status**:
  - Run ID: Pending dispatch / app registration confirmation
  - Measured T / M / K: Pending board report
