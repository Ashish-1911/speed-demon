"""Record the measured architecture batch before its first official submission."""
from astra import ROOT
details={
'a':('Contiguous latency dataflow','Replace per-group growable latency vectors with compact parsed tuples, per-group counts and one disjoint scatter into a contiguous latency arena. Exact nearest-rank selection and sorted output remain; writes now retry EINTR and partial writes.','.085440 → .083995','68,005,888 → 49,422,336','399 → 424','18.3746 → 19.5743','62.601620'),
'c':('Bounded partition ownership','Six parsing producers dispatch byte-range name references to six exclusive dictionary owners through bounded SPSC batch rings. This eliminates per-record partition locking without duplicating dictionaries. Release/acquire publication, completion ordering and mapping lifetime preserve exactness; raw escaped interiors remain an injective representation under spec 2.2.','.108081 → .088467','20,975,616 → 21,786,624','502 → 565','16.2769 → 17.5412','60.613405'),
'd':('Cache-local graph construction','Coarse vertex-range partitioning gives each worker disjoint small cursor arrays and edge intervals. This replaces graph-wide random degree/cursor writes. Forward/reverse CSR, sparse state, radix heaps and exact bidirectional Dijkstra are retained.','.511525 → .367220','268,476,416 → 217,022,464','426 → 421','10.7474 → 13.2593','56.786124')}
for t,(title,description,T,M,S,K,P) in details.items():
    p=ROOT/f'track-{t}/PERF.md'
    p.write_text(p.read_text()+f'''\n\n## November 1 architecture campaign: {title}

Baseline source: `f153bef6550d23ce7547629b7472c0ac68338a31`. This entry supersedes older statements about current best runs, pending experiments and Docker availability; historical measurements above remain historical.

{description}

Seven alternating native WSL runs after two warmups, GCC15.2 with the track's actual Dockerfile flags, Linux-local resident data: median time **{T} s**. Independent three-run median cgroup-v2 peak: **{M} bytes**. Canonical clang-format23.1.3/scc4.1.0 source lines: **{S}**. Dependency axis remains 1. Complete local K: **{K}**. These are local measurements, not official scores. Applying measured ratios to the best official baseline projects {P} machine points; actual official results must replace this projection.

Correctness: exact reference hashes for tiny/sample/full seed1, sample seeds42/99 and a new full seed20261101, repeated deterministic runs, targeted adversarial tests, ASan/UBSan and TSan. Final production-flag binaries passed the dataset suite. Offline Docker full-seed1 check passed using pinned allowlisted images, offline build, read-only runtime, unprivileged user, memory/pid limits and no network. Docker Desktop is now working.

Evidence: `engineering/evidence/astra/` (per-run timings, kernel memory, static axes, full K, output hashes, sanitizers and Docker logs). Architecture proof, cost models, rejected alternatives and measured byte-reading floors are in `ASTRA_CAMPAIGN.md`. Official result: pending the first submission of this architecture batch; no official improvement is claimed yet.
''')
(ROOT/'PROJECT_MEMORY/SPEED_DEMON_HANDOFF.md').write_text('''# Speed Demon architecture handoff — November 1

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Production baseline: `f153bef6550d23ce7547629b7472c0ac68338a31` on `speed-demon`.
- Verified architecture batch prepared in `D:/hackathons/Speed Demon-astra`, branch `codex/astra-architecture`; source commits pending. See `ASTRA_CAMPAIGN.md` and `engineering/evidence/astra/`.
- Existing rollback tag `astro-scored-ccdbb2a` is preserved. Preserve unrelated untracked `scratch/` and passport JSON.
- Baseline snapshot 2026-11-01T05:32:13Z: rank73; fifth81.182957; A/C/D best portfolio57.350057.

| Track | Best official run | T seconds | M bytes | S | K | Points |
|---|---|---:|---:|---:|---:|---:|
| A | run-4a61d3b39439 | .102003 | 68935680 | 399 | 16.771163 | 61.228159 |
| C | run-44bf4f0a8ed0 | .130768 | 18784256 | 502 | 15.127926 | 58.988969 |
| D | run-78cff558089d | .483557 | 265428992 | 426 | 11.079054 | 52.225134 |

A/D best runs use f153bef. C best uses ccdbb2a, whose C source is identical; latest f153bef C run-6bc9fdb4f566 has58.643614 points. These are distinct runs.

Implemented: A contiguous latency arena (local K+6.5%); C bounded producer/owner pipeline (+7.8%); D vertex-bucket CSR builder (+23.4%). Complete-K local projection portfolio59.951, not official. D degree-two contraction rejected: extra preprocessing/wide edges made it slower and larger. C full sort/reduce rejected by memory/amortization cost model; SCC/landmarks deferred on short-query cost model. Do not repeat prior rejected twelve-worker A, two-pass D, priority-frontier D, source-order D, cached radix tops, non-inserting state, duplicated C dictionaries or minor lock tweaks.

All three selected implementations passed tiny/sample/full1, sample42/99, full20261101, repeated SHA comparisons, targeted edge tests, ASan/UBSan, TSan, sparse growth/epoch-wrap tests and offline Docker full1 checks. Final native release binaries also passed the complete suite. No hardware counters available; measured stage/work counts and byte-scan floor are recorded instead.

Docker Desktop29.2.1 is running after reversible preservation/recreation of stale runtime socket directories. WSL data and binaries persist in `/home/mumukshu/sd-astra` (do not use ephemeral `/tmp` for checkpoints). Docker durations are not scoring times. Repo is public with MIT license; image pins and dependency weight unchanged.

Next executable task: commit each verified track, commit evidence, fast-forward unchanged production, push once, then retrieve exact-commit official A/C/D results. Keep/revert from measured K. Update this handoff and each PERF with run IDs and final source before concluding. Freeze: November2 15:00UTC /20:30IST.
''')
