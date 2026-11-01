# Track D: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | (official baseline) | 72.69 | 851.5 | 0 |
| 1 | Baseline C++20 | (local verification) | 9.56 | 30 (image MB) | Baseline |
| 2 | Parallel 6-worker Dijkstra queries on immutable CSR + sequential ordered emit + -flto | run-c2d04a261905 | 2.2107 | 184.84 | 39.96 (K=6.30) |
| 3 | Exact O(1) early rejection (out_degree[s]==0, in_degree[t]==0 via 217KB bitset) | run-3395c8a050e6 | 1.5937 | 185.01 | 43.34 (K=7.36) |
| 4 | Exact Bidirectional Dijkstra + cache-aligned 4-ary heap + parallel CSR construction | run-b223180e56c9 | 0.6748 | 622.16 | 45.42 (K=8.10) |
| 5 | Zero-temp parallel CSR + packed 32-bit epoch_fb + strict nd >= best heap pruning | run-1ea8409eec44 | 0.6954 | 503.60 | 45.64 (K=8.18) |
| 6 | Fast-path CSR parser (skip weight parsing in Pass 1 + branchless digit scanning) | run-1471543ef2e2 | 0.6276 | 527.92 | 46.56 (K=8.53) |
| 7 | Single-pass parallel CSR construction + thread-local scatter offsets | run-8484c76cbe41 | 0.585252 | 566.702080 | 46.975447 (K=8.699793) |

### Verification Evidence & Measurement Log

- **Official Liveboard Telemetry (Team: Kujo Jotaro, Commit 3b48b29)**:
  - Run ID: `run-1471543ef2e2`
  - Commit: `3b48b29194eff5873cbbf5cce1bc9fef39bbfea5`
  - Wall Time (T): **0.627625 s**
  - Peak Memory (M): **527,921,152 bytes** (~527.92 MB)
  - Source Lines (S): **407**
  - Dependencies (D): **1**
  - K Efficiency Coefficient: **8.533448**
  - Machine Points: **46.556227**
  - All gates passed: `check`, `build`, `correctness`, `timeout`, `sandbox` (100% OK)

- **Step 7 Bottleneck Profiling & Single-Pass CSR Construction**:
  - Detailed profiling of Step 6 on the 5,000,000-edge graph revealed that CSR graph construction took **373.78 ms** (48.8% of total process wall time).
  - Profiling breakdown:
    - Pass 1 degree counting: 136.71 ms
    - Prefix sums & offsets: 136.40 ms (caused by column-major access across 12 temporary vectors with 20M cache misses)
    - Pass 2 CSR edge writing: 100.56 ms (re-reading 88 MB text file from mmap)
    - Temporary memory during build: 336 MB (`worker_counts_f`, `worker_counts_r`, `worker_offset_f`, `worker_offset_r`).
  - Architecture Redesign:
    1. Single-pass parallel parsing: text file is scanned only once. Raw edges are stored in per-worker vectors and node degrees are counted concurrently in a single pass.
    2. Parallel prefix sums across node chunks.
    3. Thread-local scatter offsets: eliminated `worker_offset_f` and `worker_offset_r` completely; each thread computes its local write offsets and scatters edges directly into final `edges` and `r_edges`.
    4. Immediate deallocation: all temporary edge lists and degree vectors are freed (`vector::swap`) before queries begin.
  - Measured local graph construction time dropped from **373.78 ms** to **255.44 ms** (-118.34 ms). Total local runtime dropped from **766 ms** to **549 ms** (**1.40x local speedup**).
  - Docker container execution verified in 1.81 s (PASS).

- **Multi-Scale Correctness Gates**:
  - Tiny (40 queries): `ecaca77d3221fe3928109da36480cfdb39f288f2e57ecbc61d3d7706d5cb4bd5` (PASS)
  - Sample (200 queries): `a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0` (PASS)
  - Full seed 1 (2,000 queries): `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` (PASS)
  - Fresh seed 42 (Sample scale): `e003ecd1553a9010cca836975f759aa8588403070e83ed359cc3fd991a2275a6` (PASS)
  - Fresh seed 99 (Sample scale): `7d8a37adcc1fdb2bd8668d47513e42ad8f4459d363553cba96a2cd32994bf9cb` (PASS)
  - Fresh seed 777 (Sample scale, on-the-fly generated): `c941014791da5ac215bfdd01235dd6b67fe5325c4067d907977cdcf4da9c809b` (PASS)
  - All 14 test suites in `official/tests/test_d_traverse.py`: PASS
  - Docker Container Check (`check.py` on full scale): PASS (1.81 s, image 30 MB, all FROM allowlisted)


## October 31 controlled campaign

Baseline source: `a3ab590f3dfe59eaa69afdc0fd8094814dfdd573`. Historical best: `run-8484c76cbe41`, T=0.585252 s, M=566702080 bytes, S=411, D=1, K=8.699793, 46.975447 points. Latest baseline-tree run: `run-8484c76cbe41`, 46.975447 points. Historical best and latest are distinct measurements.

Experiment: **64-bit monotone radix queues for bidirectional Dijkstra**. Seven alternating local process runs on WSL i7-13650HX, GCC 15.2, `-O3 -flto -march=haswell -pthread`, input/output on Linux tmpfs: median 0.538744 -> 0.477418 s (11.4% reduction). Raw measurements: `engineering/evidence/d-radix.json` under engineering/evidence. These are not official times. GNU time RSS includes mapped input pages; it is not the board memory axis.

All existing tiny/sample/full seed-1 and sample seeds 42/99 outputs matched reference SHA-256. Offline Docker check passed full seed 1 with allowlisted images and the 8 GiB sandbox. ASan/UBSan and TSan tiny runs passed. Fresh full-scale and targeted test details are recorded in the campaign evidence.

Correction to Step 7 projection: actual run `run-8484c76cbe41` scored 46.975447, T=0.585252 s, M=566702080, S=411, D=1, K=8.699793. The earlier >50 estimate was not achieved. Lazy distance initialization was rejected (0.542397 -> 0.546646 s, no RSS reduction). Worker-count screening was inconclusive on K and partly overlapped correctness work; no worker-count change was accepted. Radix-heap rationale: https://www.princeton.edu/~alaink/Orf467F11/AhujaRadixHeap.pdf . Nonnegative weights preserve monotone insertion independently in each search direction; stale entries and lower-bound stopping remain unchanged.

Second and third isolated passes: sparse per-query states keyed by vertex (with growing exact hash table), followed by reuse of dead degree arrays as scatter cursors. All offsets are prepared and joined before scatter starts; workers then own disjoint CSR spans and separate cursor rows. Comparisons in `engineering/evidence/d-sparse.json` and `engineering/evidence/d-reuse.json` must be read within each alternating-run window. Local RSS falls from approximately 662 MB to 340?357 MB. The earlier 16-bit epoch is replaced by a 32-bit state generation with explicit wrap clearing. Final canonical S=426, D=1.

### Confirmed selected implementation

Commit `ccdbb2a64b0b6335df8709a03c9d68834443fc8f`, run `run-b50731622d5a`: T=0.493443 s, M=253067264 bytes, S=426, D=1, K=11.072623, **52.212525 machine points**. Every official gate passed. This supersedes the local projections for the selected changes.

### Phase IV rejected experiment

Queries were internally sorted by source vertex to improve CSR locality, with results written back by original query index. The output remained exact, but six alternating full-data runs showed only a small approximately **1–2%** local improvement, below the confidence and championship-impact threshold; the candidate was rejected.

Final two-pass raw-edge-elimination experiment was not selected: paired medians 0.551639 -> 0.545623 seconds, overlapping distributions, small RSS reduction. See engineering/evidence/d-twopass.json.


## November 1 architecture campaign: Cache-local graph construction

Baseline source: `f153bef6550d23ce7547629b7472c0ac68338a31`. This entry supersedes older statements about current best runs, pending experiments and Docker availability; historical measurements above remain historical.

Coarse vertex-range partitioning gives each worker disjoint small cursor arrays and edge intervals. This replaces graph-wide random degree/cursor writes. Forward/reverse CSR, sparse state, radix heaps and exact bidirectional Dijkstra are retained.

Seven alternating native WSL runs after two warmups, GCC15.2 with the track's actual Dockerfile flags, Linux-local resident data: median time **.511525 → .367220 s**. Independent three-run median cgroup-v2 peak: **268,476,416 → 217,022,464 bytes**. Canonical clang-format23.1.3/scc4.1.0 source lines: **426 → 421**. Dependency axis remains 1. Complete local K: **10.7474 → 13.2593**. These are local measurements, not official scores. Applying measured ratios to the best official baseline projects 56.786124 machine points; actual official results must replace this projection.

Correctness: exact reference hashes for tiny/sample/full seed1, sample seeds42/99 and a new full seed20261101, repeated deterministic runs, targeted adversarial tests, ASan/UBSan and TSan. Final production-flag binaries passed the dataset suite. Offline Docker full-seed1 check passed using pinned allowlisted images, offline build, read-only runtime, unprivileged user, memory/pid limits and no network. Docker Desktop is now working.

Evidence: `engineering/evidence/astra/` (per-run timings, kernel memory, static axes, full K, output hashes, sanitizers and Docker logs). Architecture proof, cost models, rejected alternatives and measured byte-reading floors are in `ASTRA_CAMPAIGN.md`. Official result: pending the first submission of this architecture batch; no official improvement is claimed yet.
