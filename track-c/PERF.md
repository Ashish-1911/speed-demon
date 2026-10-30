# Track C: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 14.44 | 400 | 0 |
| 1 | Minimal C++20 baseline (direct CSV state-machine parsing, BOM handling, exact cents arithmetic, hash join) | Pending board dispatch | (local full: 6.5s) | 30 (image MB) | Pending board run |
| 2 | Read-only mmap input + direct region array lookup + zero-copy string_view name interning | Pending board dispatch | (local full: 8.44s containerized, 3.05s user CPU) | 30 (image MB) | Exact match, -26% code size |

### Verification Evidence & Measurement Log

- **Docker Image Size**: 30 MB (verified with `docker image inspect`, allowlist compliant)
- **Local Timings (containerized with check.py on Step 2)**:
  - Tiny (1,000 records): 0.45 s (SHA-256 `e4e43bc39920e9d446c3fcd700fdd35b3959e9495c6aaa738491df38a1aed733` — PASS)
  - Sample (332,000 records): 1.55 s (SHA-256 `9035fed36823232dd41ae2d1b9d6d21aa7c73f49d8e95647e953a89d5ebe9a66` — PASS)
  - Full (3,320,000 records): 8.44 s containerized, 3.05 s process user CPU (SHA-256 `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` — PASS)
- **Fresh-seed Generalization (Sample scale)**:
  - Seed 42: PASS (exact byte match against reference)
  - Seed 99: PASS (exact byte match against reference)
- **Official Liveboard Status**:
  - Run ID: Pending dispatch / app registration confirmation
  - Measured T / M / K: Pending board report
