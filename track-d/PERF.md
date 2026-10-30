# Track D: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 72.69 | 400 | 0 |
| 1 | Baseline C++20 | (local verification) | 9.56 | 30 (image MB) | Baseline |
| 2 | Parallel 6-worker Dijkstra queries on immutable CSR + sequential ordered emit + -flto | run-c2d04a261905 | 2.2107 | 184.84 | 39.96 (K=6.30) |

### Verification Evidence & Measurement Log

- **Official Liveboard Telemetry (Team: Kujo Jotaro)**:
  - Run ID: `run-c2d04a261905`
  - Commit: `79152f70481752602411e2ca6de3332d50c748b3`
  - Wall Time (T): 2.210724 s
  - Peak Memory (M): 184,840,192 bytes (~176.28 MB)
  - Source Lines (S): 188
  - Dependencies (D): 1
  - K Efficiency Coefficient: 6.297634
  - Points: 39.958870
- **Docker Image Size**: 30 MB (verified with `docker image inspect`, allowlist compliant)
- **Local Timings (containerized with check.py on Step 2)**:
  - Tiny (40 queries): 0.35 s (SHA-256 `ecaca77d3221fe3928109da36480cfdb39f288f2e57ecbc61d3d7706d5cb4bd5` — PASS)
  - Sample (200 queries): 0.72 s (SHA-256 `a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0` — PASS)
  - Full (2,000 queries): 4.52 s containerized, 4.60 s process real wall clock (SHA-256 `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` — PASS)
- **Fresh-seed Generalization (Sample scale)**:
  - Seed 42: PASS (exact byte match against reference)
  - Seed 99: PASS (exact byte match against reference)
