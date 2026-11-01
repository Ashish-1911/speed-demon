# November 1 architecture campaign

Scope: A/C/D only. Starting production commit `f153bef6550d23ce7547629b7472c0ac68338a31`; isolated branch `codex/astra-architecture`. Track B and registration are unchanged. Local measurements below are **not official scores**.

## Baseline and limits

The 2026-11-01T05:32:13Z feed placed Kujo Jotaro 73rd. A/C/D best machine points were 61.228159 / 58.988969 / 52.225134, geometric mean **57.350057**. Fifth place was **81.182957**. This is a large gap; the implemented changes do not project top-five performance. Human judging is separate (35% of final track totals).

Independent native stage instrumentation, GCC 15.2 on WSL/i7-13650HX, Linux-local resident input:

| Track | Measured bottleneck | Architectural implication |
|---|---|---|
| A | 53–59 ms parse/aggregate; ~4 ms dictionary reconciliation; 20–23 ms reduction/output; ~93 ms whole | Percentile-only work cannot deliver a several-fold whole-program gain. Remove per-group allocations and duplicated latency capacity. |
| C | 13–16 ms serial quote-boundary scan; 74–95 ms normal parse/aggregate; ~101 ms whole with substantial scheduling variation | Shared dictionary writes and parsing dominate. Exclusive ownership can change this without duplicating dictionaries. |
| D | 113–116 ms parse/count; 13–18 ms prefix; 89–105 ms scatter; 207–212 ms queries; 437–455 ms whole | Query acceleration alone leaves roughly half the baseline. Random graph-wide build writes are a major independent target. |

The minimal AVX2 newline-count experiment reads 239/202/92 MB for A/C/D. Six-worker median scan times are 5.93/4.94/1.99 ms (about 40/41/46 GB/s). This is an optimistic **byte-reading floor**, not a proven lower bound for parsing, hashing, selection or graph traversal; it excludes their required dependencies and writes. Existing programs are not at raw input-bandwidth saturation. No supported WSL cycles/instructions events were available; cache-miss and instruction counts are not claimed.

Instrumented D executes 7,123,602 vertex expansions, 20,971,315 edge examinations and 450,173 stale queue removals over the public full queries. These are actual work counts, not inferred hardware counters. Sparse query state and exact bidirectional termination remain useful.

## Candidate matrix and decisions

| Track / architecture | Work removed | Added work / memory | Exactness risk | Decision / plausible gain |
|---|---|---|---|---|
| A contiguous parsed tuples and counted scatter | Per-group growable vectors, their slack and repeated merge allocation | One 8-byte tuple per record, count prefix, disjoint scatter into one latency arena | Mapping local IDs to sorted global groups; rank boundaries | Implemented. Local complete K +6.5%, primarily memory. |
| A online owner queues | Cross-worker merge | Dispatch every record and retain exact latencies anyway | Ownership and skew | Not implemented: unlike C, existing A merge is a small fraction and exact percentile storage remains. |
| A radix/multi-rank selection | Some comparison/partition passes | Histograms and additional passes across many small groups | Full uint32 range, nearest ranks | Deferred: selection/output together only ~22 ms; retain exact sort/nth-element. |
| C bounded producer/owner pipeline | Per-record contended partition locks and shared writes | 36 SPSC queues, each four 256-reference blocks; ~0.9 MB | Queue publication, completion, lifetime, partition ownership | Implemented; local complete K +7.8% after source penalty. |
| C full sort-and-reduce | Shared dictionary insertion | 3.32M x 24-byte references ~80 MB; radix reorder another ~80 MB | Canonical equality, countries | Cost-model rejection, not a measured implementation: ~160 MB versus ~19 MB official baseline requires roughly 2.4x runtime speedup just to offset memory before source cost. Existing hashing overlaps parsing. |
| C duplicated thread dictionaries | Contention | Repeated dictionary memory and merge | Exact deduplication | Previously rejected; not repeated. |
| D two-neighbor chain contraction | Traversal of eligible internal vertices | Chain detection/walks and 64-bit compact edges | Protect query endpoints; directional sums; self-loops | Implemented and rejected. Removed 397,554 vertices but median .551 s versus .493 s and higher RSS. |
| D coarse vertex-range partitioning | Full-graph random degree/cursor traffic | Histograms and two sequential coarse scatter stages; reused 60 MB partition buffer | Disjoint ranges, directed reverse edges, empty buckets | Implemented. Local complete K +23.4%; no query algorithm change. |
| D SCC rejection | Searches for unreachable pairs | Full-graph SCC preprocessing | Directed condensation reachability | Generator topology has a giant bidirectional body; 1,950/2,000 ordinary reachable queries and only a small special tail. Existing zero-degree checks cheaply handle many exceptions. No claimed measured SCC decomposition. Weak return on preprocessing. |
| D landmarks / hierarchical search | Some of 21M query edge examinations | At least forward/reverse full-graph SSSP per directed landmark plus storage and lower-bound evaluations | Admissibility and directed termination | Deferred by cost model: low query count, short rank-biased queries, substantial preprocessing; no claimed benchmark or optimality proof. |

Primary research context: [Efficient Aggregation](https://arxiv.org/abs/2010.00152), [Global Hash Tables Strike Back](https://arxiv.org/abs/2505.04153), and [out-of-core partitioned aggregation](https://hannes.muehleisen.org/publications/icde2024-out-of-core-kuiper-boncz-muehleisen.pdf). These motivate comparisons, not performance guarantees for this workload.

## Selected implementation and correctness

A records compact local group IDs and latencies while accumulating counts/errors. A prefix sum assigns each worker a disjoint interval in each global group. Parallel scatter writes every input latency exactly once, including duplicates. The unchanged exact selection works on group spans, and sorted global endpoint IDs preserve output order. Partial output writes and EINTR are handled.

C assigns each dictionary partition to exactly one consumer. Each producer/consumer pair has a bounded single-producer/single-consumer ring; release/acquire transfers populated blocks and permits reuse only after consumption. Completion is acquired before testing the final queue slot, so an owner cannot exit before observing the last publication. All threads join before distinct counts are read. Names reference live input mappings. Under frozen spec 2.2, every nonempty name is quoted and doubled-quote escaping is injective: equal raw interiors imply exactly equal canonical values and vice versa. Empty quoted/unquoted names both have length zero. Byte comparison resolves hash collisions, and partition overflow remains exact.

D first parses compact raw edges. Per-worker coarse histograms establish disjoint bucket intervals. A single worker then owns each small vertex range and its edge interval, counting and scattering with an 8,192-entry cursor array. Forward and reverse graphs use the same mechanism and reuse the temporary buffer. No search approximation, dropped edge, altered weight or endpoint remapping is introduced. Parallel-edge order is immaterial to exact Dijkstra.

Passed: tiny, sample, sample seeds 42/99, public full seed 1, independently generated **full seed 20261101**, repeated exact reference hash comparisons, empty A, percentile boundary/uint32/long-endpoint tests, C hostile CSV and forced partition overflow with queue wrap, twelve random all-pairs D graphs, zero edges, bucket boundaries, zero/max weights, self-loops/duplicates, sparse growth and explicit uint32 epoch wrap. ASan/UBSan and TSan passed targeted suites. Final production-flag binaries repeat the dataset/adversarial comparisons. All three offline Docker checks passed full seed 1 using allowlisted pinned GCC15/Debian13 images, no build/run network and the checker restrictions.

Fresh full SHA-256:

- A: `7e632eb5e25d015275a769b992d8bd38f9489910b6afe14e3c030bcad4e0e5bc`
- C: `baa7f27b3f9aaba45dca120846a7ca4abb01dbb9e15d8d782769df0ed7bac9ea`
- D: `96305713a030321636048ae78f28a6d1716f5e980c1d2b58e6014ceddc1bb9f6`

## Controlled final native comparison

Seven alternating timed runs after two warmups per binary, compiler flags taken from the actual Dockerfiles. Independent median of three cgroup-v2 peaks; input resident before each new cgroup. Process elapsed includes launch and GNU-time overhead. Memory is kernel-charged memory, not GNU RSS (which includes shared input pages). The canonical formatter 23.1.3 and scc 4.1.0 were downloaded with verified pinned hashes. No added dependencies; D=1 throughout.

| Track | T baseline → selected (s) | M baseline → selected (bytes) | S baseline → selected | Local K baseline → selected |
|---|---:|---:|---:|---:|
| A | .085440 → .083995 | 68,005,888 → 49,422,336 | 399 → 424 | 18.3746 → 19.5743 |
| C | .108081 → .088467 | 20,975,616 → 21,786,624 | 502 → 565 | 16.2769 → 17.5412 |
| D | .511525 → .367220 | 268,476,416 → 217,022,464 | 426 → 421 | 10.7474 → 13.2593 |

Time ranges and raw per-run data are in `engineering/evidence/astra/*-selected.json`; full scoring calculation is `complete-K.json`. Ratio projections against the best official baseline yield 62.602 / 60.613 / 56.786 points and portfolio 59.951, **not official results**. Different CPUs, scheduling and memory accounting can alter these gains. Official outcomes must supersede these estimates.

GCC15 LTO remains enabled. C/D use AVX2-compatible Haswell targeting. No AVX-512, PGO training, new dependencies or build complexity was introduced. A final comparison uses its unchanged generic compiler flags rather than conflating the initial Haswell screening with production measurements.

## Reproduction and environment

Scripts are under `engineering/astra*.py`; selected source is in the three track folders. Generator scripts anchor to `f153bef` so they remain reproducible after integration. Linux data/binaries live outside the repository at `/home/mumukshu/sd-astra`. Do not run the old four-track campaign scripts for this task. Evidence files contain hashes, stage/work measurements, sanitizer results, canonical counts and Docker logs; no datasets or binaries are committed.

Docker Desktop 29.2.1 now runs. Its startup failure came from stale AF_UNIX runtime socket directories. Those directories were renamed and preserved, then recreated; no credentials or user data were inspected or deleted. Preserve unrelated `scratch/` and the user's untracked passport JSON. The existing rollback tag remains intact.

## Official outcome

Pending submission of the verified architecture batch. Update this section and the handoff from exact-commit board results; never substitute projected scores for the final source's measurements.
