# Track C: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 14.44 | 400 | 0 |
| 1 | Minimal C++20 baseline (direct CSV state-machine parsing, BOM handling, exact cents arithmetic, hash join) | Pending board dispatch | (local full: 6.5s) | 30 (image MB) | Pending board run |

### Verification Evidence & Measurement Log

- **Docker Image Size**: 30 MB (verified with `docker image inspect`, allowlist compliant)
- **Local Timings (containerized with check.py)**:
  - Tiny (1,000 records): 0.45 s (SHA-256 `e4e43bc39920e9d446c3fcd700fdd35b3959e9495c6aaa738491df38a1aed733` — PASS)
  - Sample (332,000 records): 1.66 s (SHA-256 `9035fed36823232dd41ae2d1b9d6d21aa7c73f49d8e95647e953a89d5ebe9a66` — PASS)
  - Full (3,320,000 records): 6.53 s (SHA-256 `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` — PASS)
- **Fresh-seed Generalization (Sample scale)**:
  - Seed 42: 4.06 s (SHA-256 `0b2db0295de7e26e2b9547f1ce823087b8b6a1988ec5353e2ad44b6416c3f690` — PASS)
  - Seed 99: 1.69 s (SHA-256 `32e1f5ee7553477a289c846129bc0356bafdb0904502e13b418f114d110d680c` — PASS)
- **Official Liveboard Status**:
  - Run ID: Pending dispatch / app registration confirmation
  - Measured T / M / K: Pending board report
