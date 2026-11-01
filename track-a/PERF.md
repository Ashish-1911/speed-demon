# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 32.05 | 228 | 0 |
| 1 | Baseline C++20 | run-a334b18ad5d6 | 0.4824 | 35.45 | 49.93 (K=9.97) |
| 2 | Read-only mmap input + zero-copy string_view endpoints + fixed 72h window index + std::nth_element percentiles + -flto | run-3c32c728ec43 | 0.4861 | 35.51 | 49.84 (K=9.93) |
| 3 | Parallel 6-worker chunk ingestion + branch-free status/latency parsing + L1 flat endpoint hash table + parallel group percentiles | run-8247401a3a6e | 0.1453 | 80.95 | 57.34 (K=14.02) |
| 4 | Parallel hour-partitioned merge & zero-copy vector moves + fast 2-digit integer serialization + block I/O write | run-593ffd6b22fb | 0.1138 | 68.87 | 60.05 (K=15.89) |

### Verification Evidence & Measurement Log

- **Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 3b48b29)**:
  - Run ID: 
un-593ffd6b22fb
  - Commit: 3b48b29194eff5873cbbf5cce1bc9fef39bbfea5
  - Wall Time (T): 0.113807 s (vs 0.143051 s on Step 3 baseline: 1.26x official speedup)
  - Peak Memory (M): 68,870,144 bytes (~68.87 MB, reduced by 12 MB)
  - Source Lines (S): 398
  - Dependencies (D): 1
  - K Efficiency Coefficient: 15.88661 (up from 14.133606: +1.75 jump)
  - Points: 60.051562 (up from 57.512648: +2.54 points)
  - All Gates: check=true, build=true, correctness=true, timeout=true, sandbox=true.

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


## October 31 controlled campaign

Baseline source: `a3ab590f3dfe59eaa69afdc0fd8094814dfdd573`. Historical best: `run-b34e80cbe49f`, T=0.11331 s, M=68870144 bytes, S=398, D=1, K=15.921413, 60.099081 points. Latest baseline-tree run: `run-83e1bef8d6e1`, 57.934474 points. Historical best and latest are distinct measurements.

Experiment: **Fuse endpoint scanning and hashing; create output for empty input**. Seven alternating local process runs on WSL i7-13650HX, GCC 15.2, `-O3 -flto -march=haswell -pthread`, input/output on Linux tmpfs: median 0.088839 -> 0.080624 s (9.2% reduction). Raw measurements: `engineering/evidence/a-fused.json` under engineering/evidence. These are not official times. GNU time RSS includes mapped input pages; it is not the board memory axis.

All existing tiny/sample/full seed-1 and sample seeds 42/99 outputs matched reference SHA-256. Offline Docker check passed full seed 1 with allowlisted images and the 8 GiB sandbox. ASan/UBSan and TSan tiny runs passed. Fresh full-scale and targeted test details are recorded in the campaign evidence.

### Confirmed selected implementation

Commit `ccdbb2a64b0b6335df8709a03c9d68834443fc8f`, run `run-6aa7f879d0cd`: T=0.106657 s, M=69038080 bytes, S=399, D=1, K=16.396307, **60.737302 machine points**. Every official gate passed. This supersedes the local projections for the selected changes.

### Phase III local experiment (rejected)

The worker-count candidate increased the parser/reduction workers from 6 to 12. On eight alternating full-dataset runs using GCC 15.2, `-O3 -flto -march=haswell -pthread`, median local wall time changed from **0.743 s to 0.605 s**; median RSS changed from **300,934 KB to 342,070 KB**. Tiny, sample, seed 42, seed 99 and full output hashes matched the recorded reference hashes. The change is integrated pending an official liveboard run; the Docker checker could not run because Docker is unavailable in the current WSL environment.

Official run `run-b68c2aba294b` for commit `1b99e94d1525f3a02dfdf1641e1d23afff4c1e72` passed every gate but measured `T=0.106818 s`, `M=112234496 bytes`, `K=14.866576`, and **58.610548 points**. The memory increase outweighed the local runtime gain, so production remains at six workers and the prior official run `run-6aa7f879d0cd` remains the Track A best.

The restored six-worker source was subsequently scored as commit `d2aba076b028a1f7bc69b5b45e080376003194d1` in `run-a7a9c1ef5deb`: `T=0.105859 s`, `M=68440064 bytes`, `S=399`, `K=16.486653`, and **60.856624 points**. All official gates passed; this is now the Track A best.


## November 1 architecture campaign: Contiguous latency dataflow

Baseline source: `f153bef6550d23ce7547629b7472c0ac68338a31`. This entry supersedes older statements about current best runs, pending experiments and Docker availability; historical measurements above remain historical.

Replace per-group growable latency vectors with compact parsed tuples, per-group counts and one disjoint scatter into a contiguous latency arena. Exact nearest-rank selection and sorted output remain; writes now retry EINTR and partial writes.

Seven alternating native WSL runs after two warmups, GCC15.2 with the track's actual Dockerfile flags, Linux-local resident data: median time **.085440 → .083995 s**. Independent three-run median cgroup-v2 peak: **68,005,888 → 49,422,336 bytes**. Canonical clang-format23.1.3/scc4.1.0 source lines: **399 → 424**. Dependency axis remains 1. Complete local K: **18.3746 → 19.5743**. These are local measurements, not official scores. Applying measured ratios to the best official baseline projects 62.601620 machine points; actual official results must replace this projection.

Correctness: exact reference hashes for tiny/sample/full seed1, sample seeds42/99 and a new full seed20261101, repeated deterministic runs, targeted adversarial tests, ASan/UBSan and TSan. Final production-flag binaries passed the dataset suite. Offline Docker full-seed1 check passed using pinned allowlisted images, offline build, read-only runtime, unprivileged user, memory/pid limits and no network. Docker Desktop is now working.

Evidence: `engineering/evidence/astra/` (per-run timings, kernel memory, static axes, full K, output hashes, sanitizers and Docker logs). Architecture proof, cost models, rejected alternatives and measured byte-reading floors are in `ASTRA_CAMPAIGN.md`. Official result: pending the first submission of this architecture batch; no official improvement is claimed yet.
