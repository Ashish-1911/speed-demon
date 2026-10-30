# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 32.05 | 228 | 0 |
| 1 | Baseline C++20 | run-0c6f5ab3060d | 0.0313 | 20.28 | 81.22 (K=42.10) |
| 2 | Read-only mmap input + zero-copy string_view endpoints + fixed 72h window index + std::nth_element percentiles | (dispatched in queue) | (local full: 4.38s) | 30 (image MB) | Pending liveboard run |

### Verification Evidence & Measurement Log

- **Official Liveboard Telemetry (Best Scored)**:
  - Run ID: `run-0c6f5ab3060d`
  - Wall Time (T): 0.031311 s
  - Peak Memory (M): 20,283,392 bytes (~19.34 MB)
  - Source Lines (S): 226
  - Dependencies (D): 1
  - K Efficiency Coefficient: 42.102855
  - Points: 81.215577
- **Docker Image Size**: 30 MB (allowlist compliant multi-stage GCC 15 / debian:13-slim)
- **Local Timings (containerized with check.py on Step 2)**:
  - Tiny (1,000 lines): 0.38 s (SHA-256 `c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f` — PASS)
  - Sample (310,000 lines): 0.89 s (SHA-256 `ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3` — PASS)
  - Full (3,100,000 lines): 4.38 s containerized, 0.56 s process user CPU (SHA-256 `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` — PASS)
- **Fresh-seed Generalization**:
  - Seed 42: PASS (exact byte match against reference)
  - Seed 99: PASS (exact byte match against reference)
