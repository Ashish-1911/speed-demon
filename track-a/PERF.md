# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 32.05 | 228 | 0 |
| 1 | Minimal C++20 baseline (direct timestamp parsing, std::map grouping, nearest-rank) | Pending board dispatch | (local full: 25.2s) | 30 (image MB) | Pending board run |

### Verification Evidence & Measurement Log

- **Docker Image Size**: 30 MB (verified with `docker image inspect`, allowlist compliant)
- **Local Timings (containerized with check.py)**:
  - Tiny (1,000 lines): 0.48 s (SHA-256 `c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f` — PASS)
  - Sample (310,000 lines): 2.14 s (SHA-256 `ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3` — PASS)
  - Full (3,100,000 lines): 25.25 s (SHA-256 `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` — PASS)
- **Fresh-seed Generalization (Sample scale)**:
  - Seed 42: 2.36 s (SHA-256 `835c36377118af19264750c9b1031225ead44a8555e913c22d31a827dbe486c8` — PASS)
  - Seed 99: 2.08 s (SHA-256 `859a0eefc73cd9e06b5ff57666d31a527bf620075c65a28034e9146b57e0a964` — PASS)
- **Official Liveboard Status**:
  - Run ID: Pending dispatch / app registration confirmation
  - Measured T / M / K: Pending board report
