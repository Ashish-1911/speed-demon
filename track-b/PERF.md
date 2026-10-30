# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 56.35 | 2290 | 0 |
| 1 | Baseline C++20 (query-first flat hash table + 6-worker parallel index & query eval) | run-ef90581e290a | 1.4380 | 246.29 | 43.33 (K=7.36) |

### Verification Evidence & Measurement Log

- **Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 543d1be)**:
  - Run ID: `run-ef90581e290a`
  - Commit: `543d1be6a6d9fab017e898e6a0ebcea2972530a0`
  - Wall Time (T): 1.438019 s (vs 56.35 s reference: 39x speedup)
  - Peak Memory (M): 258,252,800 bytes (~246.29 MB)
  - Source Lines (S): 540
  - Dependencies (D): 1
  - K Efficiency Coefficient: 7.356055
  - Points: 43.332248
  - Query-first dictionary: 75,747 distinct query terms extracted and indexed in 0.058 s.
  - Multi-threaded document scanning: 250,000 documents (36.27M tokens) scanned and indexed in 3.91 s across 6 threads using open-addressing dictionary lookups.
  - Multi-threaded query evaluation: 50,000 queries (31,000 BOOL + 19,000 PHRASE) evaluated with exact candidate verification in 0.75 s across 6 threads.
  - Local process execution time: **4.67 s** total process wall time.
  - Local containerized check (`check.py` on full scale): **4.66 s** (including container initialization).

- **Multi-Scale Correctness Gates**:
  - Tiny (1,000 docs, 1,000 queries): SHA-256 `dcdad7cedfc07eb66ecced1861cc91d9017e004c56cbe4f38e9413b62b17ff70` (PASS)
  - Sample (25,000 docs, 5,000 queries): SHA-256 `eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f` (PASS)
  - Full (250,000 docs, 50,000 queries): SHA-256 `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` (PASS)
  - Fresh Seed 42 (Sample scale): SHA-256 `8afc643bfca75356178541927d54654067b8f4c288ec9c16862e20d8d459cb63` (PASS)
  - Fresh Seed 99 (Sample scale): SHA-256 `e603e9f94513c4ed0ada769340818f72a78331f6fbf38c7ed01c903930a002c2` (PASS)

- **Docker Image & Compliance**:
  - Pinned GCC 15 multi-stage build $\to$ Debian 13-slim (`allowlist.json` compliant)
  - Image size: 30 MB (cap is 2 GB)
  - Dependencies: 0
