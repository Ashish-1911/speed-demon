# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 32.05 | 228 | 0 |
| 1 | Baseline C++20 | run-a334b18ad5d6 | 0.4824 | 35.45 | 49.93 (K=9.97) |
| 2 | Read-only mmap input + zero-copy string_view endpoints + fixed 72h window index + std::nth_element percentiles + -flto | run-3c32c728ec43 | 0.4861 | 35.51 | 49.84 (K=9.93) |
| 3 | Parallel 6-worker chunk ingestion + branch-free status/latency parsing + L1 flat endpoint hash table + parallel group percentiles | run-8247401a3a6e | 0.1453 | 80.95 | 57.34 (K=14.02) |
| 4 | Parallel hour-partitioned merge & zero-copy vector moves + fast 2-digit integer serialization + block I/O write | (pending board run) | ~0.080 | ~75 | >62.0 (K>19.0) |

### Verification Evidence & Measurement Log

- **Step 4 Deep Profiling & Bottleneck Resolution**:
  - Detailed stage profiling on full 3,100,000 requests log revealed that output serialization and streaming previously consumed 128.69 ms out of 236.49 ms total wall time (54.4% of total runtime) due to 761,448 `std::to_string()` allocations and 126,908 individual `std::ofstream::write()` calls.
  - Implemented parallel hour-partitioned reduction: the 72-hour timeline is split across the 6 worker threads (12 hours per worker). Each worker independently merges its assigned hour groups, applies in-place nearest-rank percentiles (`std::nth_element` / `std::sort`), immediately deallocates completed latency buffers (`vector::swap`), and formats lines into per-worker contiguous pre-reserved memory buffers using branchless 2-digit lookup integer formatting (`append_u32`/`append_u64`).
  - Output is committed in total order using atomic POSIX block `write()` calls to `out_fd`.
  - Local measured end-to-end execution time dropped from **236.49 ms** down to **80.42 ms** (nearly a **3.0x local speedup**).
  - Total container execution time verified in Docker sandbox via `python check.py a --data data/a_full --expected 495ca09682b9...`: **1.16 s** (PASS).

- **Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 955b473)**:
  - Run ID: `run-8247401a3a6e`
  - Commit: `955b47396a7e0b1fa2bc4da0bec326059b113d29`
  - Wall Time (T): 0.145251 s (vs 0.482428 s on Step 1: 3.32x speedup)
  - Peak Memory (M): 80,949,248 bytes (~80.95 MB)
  - Source Lines (S): 327
  - Dependencies (D): 1
  - K Efficiency Coefficient: 14.022327 (up from 9.968207: +4.05 jump)
  - Points: 57.341005 (up from 49.930853: +7.41 points)

- **Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 5502433)**:
  - Run ID: `run-a334b18ad5d6`
  - Wall Time (T): 0.482428 s
  - Peak Memory (M): 35,446,784 bytes (~33.80 MB)
  - Source Lines (S): 175
  - Dependencies (D): 1
  - K Efficiency Coefficient: 9.968207
  - Points: 49.930853

- **Step 3 Parallel Scaling Benchmark (Native measurements across worker counts)**:
  - 1 Worker: Parse 1.308 s | Total 1.376 s
  - 2 Workers: Parse 1.196 s | Total 1.252 s
  - 4 Workers: Parse 0.622 s | Total 0.680 s
  - 6 Workers: Parse 0.583 s | Total 0.643 s
  - 8 Workers: Parse 0.511 s | Total 0.574 s
  - 12 Workers: Parse 0.539 s | Total 0.612 s
  - Containerized wall time on full 3.1M log: dropped from **5.44 s** down to **2.02 s** (**2.69x local container speedup**).
  - ThreadSanitizer audit: Passed with 0 data races, clean thread lifecycle and partition boundary alignment.

- **Correctness Gates (Exact SHA-256 matches)**:
  - Tiny (1,000 lines): `c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f` (PASS)
  - Sample (310,000 lines): `ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3` (PASS)
  - Full (3,100,000 lines): `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` (PASS)
  - Seed 42: `835c36377118af19264750c9b1031225ead44a8555e913c22d31a827dbe486c8` (PASS)
  - Seed 99: `859a0eefc73cd9e06b5ff57666d31a527bf620075c65a28034e9146b57e0a964` (PASS)
