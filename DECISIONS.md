# Speed Demon 2026 — Engineering Decisions & Operational Log

## Competition Identity & Scope
- **Repository Branch**: `speed-demon`
- **Remote**: `https://github.com/Mumuksh-Jain/speed-demon.git`
- **Compiler Image**: `gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce`
- **Runtime Image**: `debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad`
- **Language**: C++20 with zero third-party dependencies (`dependency_count = 0`).
- **Portfolio Status**: All 3 required tracks (A, C, D) fully implemented and passing official offline sandbox tests at tiny, sample, and full scales, plus multiple fresh seeds.

---

## Decisions & Milestones

### Decision 01: Multi-stage Docker Build for C++20
- **Context**: Solutions must compile offline (`--network=none`), fit within 2 GB image size, and use allowlisted images pinned by multi-arch digest.
- **Decision**: Multi-stage build with `gcc:15` compiling with `-std=c++20 -O3 -static-libstdc++ -static-libgcc`, copying only the single compiled binary into `debian:13-slim`.
- **Result**: Docker images for Tracks A, C, and D are each ~30 MB (far below the 2 GB cap), build in ~1.0s - 1.6s, and have zero runtime dependencies.

### Decision 02: Track A Baseline Implementation & Full Scale Verification
- **Implementation**:
  - Direct 20-character hour string truncation from fixed 27-byte RFC 3339 timestamp.
  - Linear scan delimiter parsing for method, endpoint, status, and latency without regex.
  - `std::map<std::pair<std::string, std::string>, GroupData>` providing automatic total chronological and bytewise ascending ordering.
  - Exact 1-indexed nearest-rank percentile formula `(p * n + 99) / 100` and integer floor error ppm calculation.
- **Verification Evidence**:
  - Tiny dataset (1,000 lines): SHA-256 `c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f` (PASS)
  - Sample dataset (310,000 lines): SHA-256 `ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3` (PASS)
  - Full dataset (3,100,000 lines): SHA-256 `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` (PASS in 25.25s)
  - Fresh Seed 42: SHA-256 `835c36377118af19264750c9b1031225ead44a8555e913c22d31a827dbe486c8` (PASS)
  - Fresh Seed 99: SHA-256 `859a0eefc73cd9e06b5ff57666d31a527bf620075c65a28034e9146b57e0a964` (PASS)

### Decision 03: Track C Baseline Implementation & Full Scale Verification
- **Implementation**:
  - Zero-dependency CSV state-machine handling BOM at start and mid-file, embedded quotes, and multi-line fields.
  - Exact 64-bit integer cents calculations for amount sums and min/max.
  - Region table hash join with unknown regions routed to `_UNMATCHED`.
  - String unescaping for exact UTF-8 name and note byte counting and distinct name sets.
- **Verification Evidence**:
  - Tiny dataset (1,000 records): SHA-256 `e4e43bc39920e9d446c3fcd700fdd35b3959e9495c6aaa738491df38a1aed733` (PASS)
  - Sample dataset (332,000 records): SHA-256 `9035fed36823232dd41ae2d1b9d6d21aa7c73f49d8e95647e953a89d5ebe9a66` (PASS)
  - Full dataset (3,320,000 records): SHA-256 `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` (PASS in 6.53s vs 14.44s reference)
  - Fresh Seed 42: SHA-256 `0b2db0295de7e26e2b9547f1ce823087b8b6a1988ec5353e2ad44b6416c3f690` (PASS)
  - Fresh Seed 99: SHA-256 `32e1f5ee7553477a289c846129bc0356bafdb0904502e13b418f114d110d680c` (PASS)

### Decision 04: Track D Baseline Implementation & Full Scale Verification
- **Implementation**:
  - High-performance POSIX buffered integer parser `FastReader`.
  - Compact Compressed Sparse Row (CSR) adjacency graph with degree prefix sums.
  - Epoch-indexed Dijkstra distance arrays avoiding clearing 1.7M node distance arrays between queries.
  - O(1) identity `s == t` handling and 64-bit distance precision.
- **Verification Evidence**:
  - Tiny dataset (40 queries): SHA-256 `ecaca77d3221fe3928109da36480cfdb39f288f2e57ecbc61d3d7706d5cb4bd5` (PASS)
  - Sample dataset (200 queries): SHA-256 `a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0` (PASS)
  - Full dataset (2,000 queries): SHA-256 `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` (PASS in 9.56s vs 72.69s reference)
  - Fresh Seed 42: SHA-256 `e003ecd1553a9010cca836975f759aa8588403070e83ed359cc3fd991a2275a6` (PASS)
  - Fresh Seed 99: SHA-256 `7d8a37adcc1fdb2bd8668d47513e42ad8f4459d363553cba96a2cd32994bf9cb` (PASS)

### Decision 05: Track A Optimization — Endpoint Interning & Buffered Ingestion
- **Hypothesis**: Replacing `std::getline` and 3.1M Red-Black Tree node traversals in `std::map` with a 1MB buffered reader, endpoint string interning into `uint16_t` IDs, and a 2D dense `table[hour_id][ep_id]` with a single final sort will drastically eliminate allocation overhead and improve execution time by >3x without altering output.
- **Result**:
  - Container wall time on full 3.1M lines decreased from **25.25s** to **5.26s** (a **4.8x local container speedup**).
  - Exact SHA-256 verified on Tiny, Sample, Full (`495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3`), and fresh seeds.
  - Decision: **KEPT**.

### Decision 07: Evidence-Based Deep Optimization & Regression Resolution (Tracks C, D, A)
- **Track C Regression Diagnosis & Resolution**:
  - *Diagnosis*: Profiling revealed that maintaining 242 separate `std::unordered_set<std::string_view>` instances (one per country) caused hundreds of thousands of heap node allocations and severe CPU cache misses across 3.32M records during per-country set insertions.
  - *Optimization*: Implemented a single global canonical name dictionary (`std::unordered_map<std::string_view, NameBits>`) using compact 256-bit (`uint64_t mask[4]`) country-membership bitsets. When inserting a record for country $c$, checking `(mask[c >> 6] & (1ULL << (c & 63)))` enables branch-free distinct counting without heap allocations or per-country sets.
  - *Evidence*: Local full containerized runtime dropped from **8.44 s** to **4.11 s** (a **2.05x container speedup**); process user CPU dropped from **3.50 s** to **1.51 s** (a **2.32x CPU speedup**). Verified exact SHA-256 (`d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536`) across tiny, sample, full, seed 42, and seed 99.
  - *Decision*: **KEPT**.
- **Track D Bottleneck Decomposition**:
  - *Measurement*: Parsing: 0.48 s, CSR graph construction: 0.09 s. Query execution is the dominant bottleneck (>90% of execution).
  - *Evaluation*: Verified that 6 worker threads match the competition sandbox's 6 physical P-cores with minimal cache thrashing. Preserved exact deterministic sequential output assembly.
  - *Decision*: **KEPT**.
- **Compiler Optimization (Tracks A, C, D)**:
  - *Evaluation*: Evaluated `-flto` and PGO. `-flto -O3` in GCC 15 yielded a ~28% user CPU speedup on Track A (0.558 s down to 0.402 s), while compiling in 1.1s - 1.3s offline. Added `-flto` across all three tracks' Dockerfiles.
  - *Decision*: **KEPT**.

### Decision 08: Liveboard Audit (Team: Kujo Jotaro) & Track D Profiling Reconciliation
- **Official Liveboard Alignment**:
  - Audited `board.json` against the Git repository `Mumuksh-Jain/speed-demon`. Confirmed team identity is registered as `Kujo Jotaro` (`kujo-jotaro-4ca762`).
  - Recorded genuine official Liveboard runs for commit `79152f7`:
    - Track A: `run-3c32c728ec43` ($T = 0.486$ s, $M = 35.51$ MB, $S = 175$, $K = 9.93$, Points = 49.84).
    - Track C: `run-f0261cacf78e` ($T = 1.026$ s, $M = 13.62$ MB, $S = 269$, $K = 6.32$, Points = 40.05).
    - Track D: `run-c2d04a261905` ($T = 2.211$ s, $M = 184.84$ MB, $S = 188$, $K = 6.30$, Points = 39.96).
    - Track B: Correctly un-scored by the board as `stage: unchanged` (unchanged starter reference code).
- **Track D Discrepancy Resolution**:
  - Identified that a diagnostic script parsed `queries.txt` assuming a count header, which shifted all queries by one integer and produced artificial random queries across disconnected components.
  - True production Dijkstra distribution across all 2,000 queries: settled median = 490 (generator spec predicted ~512), mean = 13,354 (generator spec ~12,300), stale pops = 6.89%, and top 5% most expensive queries account for 52.42% of CPU time.

### Decision 09: Track D Exact O(1) Early Rejection Optimization
- **Hypothesis**: The heaviest queries in Track D are those targeting unreachable nodes with zero in-degree (`in_degree[t] == 0`), which forced Dijkstra to settle ~1.74M nodes before failing. By tracking in-degree non-zero status in a 217 KB bitset (`has_in`) during CSR ingestion and checking `out_degree[s] == 0` ($O(1)$) and `in_degree[t] == 0` ($O(1)$), unreachable queries can be resolved instantly without Dijkstra exploration.
- **Evidence**:
  - Microbenchmark across all 2,000 queries on 6 threads: Query execution time dropped from 2.02 s to 1.42 s (**30% query speedup**).
  - Containerized wall time with `check.py` on full dataset: dropped from **4.52 s** to **3.52 s** (**-1.00 s**).
  - Correctness verified 100%: byte-identical SHA-256 match on tiny (`ecaca77d...`), sample (`a4c76ed0...`), full seed-1 (`a23dbdf0...`), seed 42 (`e003ecd1...`), and seed 99 (`7d8a37ad...`).
  - Source lines: +5 lines ($S = 193$). Peak memory impact: +217 KB.
- **Decision**: **KEPT & COMMITTED**.
