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

### Decision 10: Track B (Index) C++20 Full Implementation
- **Context**: Track B was previously an un-scored Python starter reference. Developing a high-performance Track B provides a 4th scored track, directly targeting replacing Track C (42.37 pts) in the championship best-3 geometric mean.
- **Architecture**:
  - Query-first term extraction: All 50,000 queries are parsed upfront, mapping ~75,747 query terms into a flat open-addressing hash table with 64-bit FNV-1a hashing. Over 90% of non-query corpus tokens are rejected with average 1.1 cache-line probes without allocations.
  - Multi-threaded document chunk indexing: 250,000 documents are indexed in parallel across 6 worker threads, and thread-local ascending postings are concatenated into globally sorted ascending vectors without sorting.
  - Linear-time two-pointer streaming merges for left-to-right Boolean evaluation (`AND`, `OR`, `NOT`).
  - Rarest-anchor candidate filtering for phrase queries, verifying only candidate document text slices.
  - Dynamic lock-free multi-threaded query answering across 6 worker threads.
- **Evidence**:
  - Official Liveboard Run: `run-ef90581e290a` (Commit `543d1be6a6d9fab017e898e6a0ebcea2972530a0`).
  - Wall Time (T): **1.438019 s** (39x speedup over 56.35 s reference).
  - Peak Memory (M): **258,252,800 bytes** (~246.29 MB).
  - Source Lines (S): **540**, Dependencies (D): **1**.
  - K Efficiency Coefficient: **7.356055**, Machine Points: **43.332248**.
  - Championship Impact: Directly replaces Track C (42.367 pts) in the best-3 geometric mean, increasing team overall machine score to **45.431 points**.
  - Full scale SHA-256 match: `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` (100% PASS).
  - Multi-scale and fresh seeds: Tiny (`dcdad7ce...`), Sample (`eeeec1e9...`), Seed 42 (`8afc643b...`), Seed 99 (`e603e9f9...`) all pass.
  - Offline Docker build passes with allowlisted GCC 15 and Debian 13-slim (30 MB image, zero dependencies).
- **Decision**: **KEPT & COMMITTED**.

### Decision 11: Track A Multi-Threaded Chunk Ingestion & L1 Endpoint Table
- **Context**: Profiling revealed that single-threaded parsing of 3.1M log records took >97% of execution time, dominated by string hashing and vector reallocations.
- **Architecture**:
  - Exact newline-aligned chunk partitioning: Divides the 238 MB memory-mapped file into 6 contiguous byte slices with exact newline adjustment so every record is parsed exactly once with zero overlap.
  - Cache-resident flat open-addressing endpoint dictionary (4,096 slots, 32 KB, L1 resident) with 64-bit FNV-1a hashing, eliminating `std::unordered_map` bucket pointer traversals.
  - Branch-free HTTP status and latency parsing exploiting fixed 3-digit status grammar.
  - Zero-lock thread-local aggregation tables: Each worker accumulates into thread-local `table[72][2048]`.
  - Parallel percentile computation: Distributes the ~126k active `(hour, endpoint)` groups across 6 threads for concurrent `std::nth_element` selection.
- **Evidence**:
  - Official Liveboard Run: `run-8247401a3a6e` (Commit `955b47396a7e0b1fa2bc4da0bec326059b113d29`).
  - Wall Time (T): **0.145251 s** (3.32x speedup over 0.4824 s baseline).
  - Peak Memory (M): **80,949,248 bytes** (~80.95 MB).
  - Source Lines (S): **327**, Dependencies (D): **1**.
  - K Efficiency Coefficient: **14.022327** (up from 9.968207: +4.05 jump).
  - Machine Points: **57.341005** (up from 49.930853: +7.41 points on Track A).
  - Championship Impact: Lifts team best-3 geometric mean machine score to **47.700 points**, advancing team ranking to Rank 35.
  - Local container wall time on full 3.1M records dropped from **5.44 s** to **2.02 s** (**2.69x local container speedup**).
  - ThreadSanitizer audit: 0 data races, clean thread lifecycle and partition boundary alignment.
  - Correctness verified 100%: exact SHA-256 match on tiny (`c24fa507...`), sample (`ca6311bb...`), full seed-1 (`495ca096...`), seed 42 (`835c3637...`), and seed 99 (`859a0eef...`).
- **Decision**: **KEPT & COMMITTED**.

### Decision 12: Track C Quote-Aware Parallel Ingestion & 512-Partition Flat Hash Dictionary
- **Context**: Profiling Track C revealed that 65% of execution time was spent in `std::unordered_map` node allocations for 100,520 distinct names, 30% in sequential CSV parsing across 3.32M records, and 5% in string allocations during `regions.csv` ingestion. Over 10% of newlines in `records.csv` are embedded inside quoted fields, making naive newline chunking invalid.
- **Architecture**:
  - Quote-aware chunk partitioning: Mathematical proof that unescaped quote parity equals total quote count mod 2 (since escaped quotes `""` add 2 to the quote count, preserving parity). Implemented an unrolled 128-byte AVX2 vector quote counter scanning 202 MB in 6 ms bare metal / 26 ms WSL, locating exact non-quoted newline boundaries for $N$ worker threads.
  - O(1) region code table: Replaced string map with 3-letter ASCII index table `uint16_t[17576]` ($26 \times 26 \times 26$), dropping `regions.csv` parse time from 184 ms to 6 ms.
  - 512-partition open-addressing flat hash dictionary: Cache-line-aligned 64-byte slots with atomic spinlocks. Zero heap allocations. Total RAM footprint across all partitions is only 33.5 MB (preserving the low memory penalty factor $M^{0.1}$).
  - SIMD quote scanning and zero-copy string views: Fast `memchr` vector scanning for note and name fields.
- **Evidence**:
  - Official Liveboard Run: `run-c120005c0caa` (Commit `f944e44d1d288f3c3a0019a73effb1d7d494f35c`).
  - Wall Time (T): **0.134218 s** (6.2x speedup over 0.8318 s baseline).
  - Peak Memory (M): **37,806,080 bytes** (~37.81 MB).
  - Source Lines (S): **473**, Dependencies (D): **1**.
  - K Efficiency Coefficient: **13.099285** (up from 7.036: +6.06 jump).
  - Machine Points: **55.862380** (up from 42.367: +13.50 points on Track C).
  - Championship Impact: Directly replaces Track B (43.35 pts) in best-3 portfolio, lifting team score.
  - Full scale SHA-256 match: `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` (100% PASS).
- **Decision**: **KEPT & COMMITTED**.

### Decision 13: Track D Exact Bidirectional Dijkstra & Parallel CSR Construction
- **Context**: Profiling the complete 2,000 queries of Track D full dataset revealed that queries with >10,000 settled vertices (22% of queries) accounted for 93.19% of total CPU time. Single-direction Dijkstra searches an expanding ball of volume $\propto R^d$, causing massive redundant exploration.
- **Architecture**:
  - Exact directed-graph Bidirectional Dijkstra:
    - Forward search from $s$ on $G$, backward search from $t$ on reverse graph $G^R$.
    - Dynamic queue selection expanding the smaller frontier.
    - Global best path length $\mu$ tracked via connecting path edges: updated on forward relaxation if neighbor was reached backward ($d_f[u] + w + d_b[v]$), and on backward relaxation if neighbor was reached forward ($d_f[v] + w + d_b[u]$).
    - Rigorous frontier termination condition: Pruning stale entries from queue tops and breaking when $top_f + top_b \ge \mu$, proving global shortest-path optimality on non-negative directed graphs.
  - Cache-aligned 4-ary min-heap: Packs 4 child priorities contiguously, reducing heap height by 50% and cutting cache misses during sift-down.
  - Parallel memory-mapped graph ingestion: Chunked line-aligned parsing across worker threads, constructing both forward and reverse CSR in parallel.
- **Evidence**:
  - Official Liveboard Run: `run-b223180e56c9` (Commit `5a64a3822e6f8e7b0ca977acb64654d2d1fdc373`).
  - Wall Time (T): **0.674803 s** (2.36x bare-metal speedup over 1.5937 s baseline).
  - Peak Memory (M): **652,386,304 bytes** (~622.16 MB).
  - Source Lines (S): **342**, Dependencies (D): **1**.
  - K Efficiency Coefficient: **8.097188** (up from 7.358379: +0.74 jump).
  - Machine Points: **45.416712** (up from 43.339108: +2.08 points).
  - Championship Impact: Elevates team overall best-3 geometric mean machine score to **52.646 points**.
  - Comprehensive 2,000-query profiling:
    - Median settled vertices: **122** vs **479** (3.9x reduction).
    - Mean settled vertices: **3,561.95** vs **11,613.6** (3.26x reduction).
    - Heap pushes: **8.65M** vs **26.58M** (3.07x reduction).
    - Query CPU time: **3,102 ms** vs **8,928 ms** (2.88x speedup).
  - End-to-end wall time: dropped from **2,791 ms** to **1,180 ms** (**2.37x speedup**).
  - Official `check.py` time: dropped from **4.52 s** to **2.36 s**.
  - Exact 100% byte-for-byte SHA-256 match across all scales and seeds: Tiny (`ecaca77d...`), Sample (`a4c76ed0...`), Full seed 1 (`a23dbdf0...`), Seed 42 (`e003ecd1...`), Seed 99 (`7d8a37ad...`).
- **Decision**: **KEPT & COMMITTED**.




### Decision 14: Track B Single-Pass Ingestion, Flat CSR Postings & Zero-Alloc Query Evaluation
- **Context**: Profiling Track B revealed that separate passes over `docs.txt` caused massive redundant memory traffic (8.5 s spent scanning doc slices on virtual memory). Furthermore, maintaining 75,747 dynamic `std::vector` posting lists led to hundreds of thousands of heap reallocations, and Boolean query operations repeatedly allocated intermediate vectors.
- **Architecture**:
  - Single-pass chunked doc slicing and tokenization: 253 MB `docs.txt` is partitioned into 6 newline-aligned chunks. Each worker thread extracts document line boundaries and immediately tokenizes document text in the same pass while cache-hot in L1/L2.
  - L1-resident 256-byte ASCII lookup tables: Cache-aligned `IS_ALNUM` and `TO_LOWER_MAP` replace multi-branch character classification loops.
  - Flat Compressed Sparse Row (CSR) Postings: 75,747 fragmented vectors are replaced by a single contiguous 84.8 MB buffer indexed by `csr_head[tid]`, cutting memory fragmentation and improving cache locality during lookups.
  - Zero-allocation ping-pong query evaluation: Thread-local reusable buffers (`bufA`, `bufB`) eliminate all dynamic vector allocations during `and_op`, `or_op`, and `not_op`.
  - Phrase term frequency sorting: Shortest posting lists are intersected first, rapidly pruning candidate documents.
  - Formatted single-block I/O: 50,000 output lines are formatted directly into a pre-sized buffer and written in a single POSIX `write()` call, dropping output I/O from 241 ms to 14 ms.
- **Evidence**:
  - Process wall time in WSL dropped from **10,073 ms to 1,485 ms** (**6.78x local process speedup**).
  - Local Docker container check on full scale dropped from **4.66 s to 2.66 s** (**1.75x container speedup**).
  - Exact 100% byte-for-byte SHA-256 match across all five scales and seeds:
    - Tiny: `dcdad7cedfc07eb66ecced1861cc91d9017e004c56cbe4f38e9413b62b17ff70` (PASS)
    - Sample: `eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f` (PASS)
    - Full seed 1: `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` (PASS)
    - Fresh seed 42: `8afc643bfca75356178541927d54654067b8f4c288ec9c16862e20d8d459cb63` (PASS)
    - Fresh seed 99: `e603e9f94513c4ed0ada769340818f72a78331f6fbf38c7ed01c903930a002c2` (PASS)
  - Allowlist-compliant multi-stage GCC 15 -> Debian 13-slim build, image size 30 MB, 0 dependencies.
- **Decision**: **KEPT & COMMITTED**.

### Decision 15: Track D Zero-Temp Parallel CSR, Packed 32-bit Epoch & Strict Heap Pruning
- **Context**: Profiling Track D revealed that peak memory was driven to 622 MB by two primary factors: (1) previous parallel graph construction retained ~157.6 MB of capacity in `worker_edges` (60 MB) and `worker_head`/`worker_rhead` (83.6 MB) because `clear()` retains buffer capacity, and (2) 12 workers $\times$ 41.77 MB per worker = 501.27 MB in distance and epoch arrays.
- **Architecture**:
  - Zero-temporary-buffer parallel CSR: Two-pass memory-mapped parser counts degree prefix offsets in Pass 1 and writes directly into final `edges` and `r_edges` in Pass 2 without allocating any intermediate edge structs, saving 157.6 MB of temporary heap allocations.
  - Packed 32-bit `epoch_fb`: High 16 bits encode forward visited epoch, low 16 bits encode backward visited epoch. Both forward and backward visited states are retrieved in a single 4-byte access, cutting per-worker epoch memory in half (from 13.92 MB to 6.96 MB per worker).
  - Strict `nd >= best` heap pruning: Prunes sub-optimal neighbor relaxations before heap insertion since non-negative edge weights ensure no path through $v$ can improve `best`.
- **Evidence**:
  - Full-scale process runtime in WSL dropped from **1,180 ms to 805 ms** (**1.47x speedup**).
  - Query execution dropped from **349 ms to 325 ms**.
  - Local Docker container check on full scale dropped from **2.36 s to 2.27 s**.
  - Peak live RSS dropped from **622 MB to ~450-520 MB** on bare metal (~588 MB in WSL).
  - Exact 100% byte-for-byte SHA-256 match across all five scales and seeds: Tiny (`ecaca77d...`), Sample (`a4c76ed0...`), Full seed 1 (`a23dbdf0...`), Seed 42 (`e003ecd1...`), Seed 99 (`7d8a37ad...`).
  - Image size: 30 MB, 0 dependencies.
- **Decision**: **KEPT & COMMITTED**.
