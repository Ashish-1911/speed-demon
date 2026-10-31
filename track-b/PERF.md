# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 56.35 | 2290 | 0 |
| 1 | Baseline C++20 (query-first flat hash table + 6-worker parallel index & query eval) | run-ef90581e290a | 1.4380 | 246.29 | 43.33 (K=7.36) |
| 2 | Single-pass parallel slice+index + Flat CSR Postings + zero-alloc ping-pong query eval + block I/O | run-09cb3c3ba99e | 1.0952 | 242.00 | 45.81 (K=8.25) |
| 3 | Document-Centric Batch Phrase Verification (radix-bucketed doc-centric tokenization + batch phrase eval) | (pending) | 0.824* | 242.00* | ~48.9* |

\* *Projected from local 24.7% wall-time reduction on full scale (740.26 ms vs 982.65 ms baseline); pending official liveboard run.*

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

- **Step 2: Single-Pass Parallel Ingestion, Flat CSR Postings & Zero-Alloc Query Evaluation**:
  - *Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 1383809)*:
    - Run ID: `run-09cb3c3ba99e`
    - Commit: `1383809ed991a0aa0b98ebfe58a2d1f05e466e06`
    - Wall Time (T): **1.095222 s**
    - Peak Memory (M): **253,755,392 bytes** (~242.00 MB)
    - Source Lines (S): **661**
    - Dependencies (D): **1**
    - K Efficiency Coefficient: **8.245800**
    - Points: **45.811641**
    - All gates passed: `check`, `build`, `correctness`, `timeout`, `sandbox` (100% OK)

- **Step 3: Document-Centric Batch Phrase Verification**:
  - *Engineering Hypothesis & Investigation*:
    - In Step 2, each phrase query independently tokenized candidate documents. Deep profiling revealed 214,226 total candidate pairs across 110,702 distinct documents (1.935 scans/doc avg, up to 19 scans), causing 417 MB of document text rescans vs 157 MB unique text (2.65x redundant rescans). Document tokenization accounted for 88.42% of phrase evaluation CPU cycles.
  - *Architecture & Implementation*:
    1. Boolean queries are evaluated directly across 12 parallel workers.
    2. Phrase queries perform rapid inverted-index candidate pruning, emitting `(doc_id, query_idx)` pairs into thread-local buffers.
    3. Candidate pairs are merged and sorted by `doc_id` using a high-speed linear-time counting/radix bucket pass (2.5 ms for all 214,226 pairs).
    4. Parallel workers partition the unique candidate documents. Each candidate document is tokenized **exactly once** (51.8 ms total tokenization time, down from 270 ms).
    5. All pending phrase queries for that document are evaluated concurrently against the single token sequence.
    6. Matches are merged, sorted, aggregated, and formatted into output buffers.
  - *Measured Performance*:
    - Local benchmark on `data/b_full` (10-run median): **740.26 ms** vs baseline **982.65 ms** (**242.39 ms wall-time reduction = 24.7% faster**).
    - Multi-scale correctness: 100% byte-exact SHA-256 match across tiny, sample, full, seed 42, and seed 99.
    - Local Docker container check (`check.py` on full scale): **1.94 s** total execution (including container start-up).
    - Source lines: 724 lines. Dependencies: 0. Docker image: 31 MB.

- **Docker Image & Compliance**:
  - Pinned GCC 15 multi-stage build $\to$ Debian 13-slim (`allowlist.json` compliant)
  - Image size: 31 MB (cap is 2 GB)
  - Dependencies: 0
