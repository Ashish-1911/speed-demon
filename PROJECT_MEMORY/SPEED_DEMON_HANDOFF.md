# Speed Demon architecture handoff — November 1

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
