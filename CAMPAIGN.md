# Controlled championship campaign

Baseline snapshot: 2026-10-31 15:47:16 UTC (21:17:16 IST). Rank 61, overall 55.273935; fifth 76.361508, gap 21.087573. This is machine-only scoring, not final judging.

Original rollback: a3ab590f3dfe59eaa69afdc0fd8094814dfdd573. Experiment branch: codex/championship-pass.

## Local experiment decisions

| Track | Experiment | Baseline median s | Candidate median s | S before/after | Conservative local K ratio |
|---|---|---:|---:|---:|---:|
| A | a-fused | 0.088839 | 0.080624 | 398/399 | 1.0493 |
| B | b-gallop | 0.683967 | 0.569268 | 722/741 | 1.0919 |
| C | c-compact | 0.100837 | 0.096994 | 502/502 | 1.0196 |
| D | d-radix | 0.538744 | 0.477418 | 411/399 | 1.0670 |

K ratios hold memory and dependencies constant; these are projections, not official scores. C reduces slots from 64 to 56 bytes (2 MiB less allocated), while D radix queues add about 2.5 MiB GNU RSS. Mapped file pages are included in GNU RSS but excluded by board memory accounting, so local RSS must not be substituted directly for official M. Source counts use SHA-verified organizer-pinned clang-format 23.1.3 and scc 4.1.0; all four baseline counts reproduce the board.

WSL i7-13650HX, GCC 15.2, Linux tmpfs inputs/output, seven alternating complete process runs. Compilation excluded; parsing, preprocessing, queries, output and process cleanup included. Hardware cycles/instructions counters unavailable in WSL. Windows-drive mmap runs were discarded for optimization decisions. Correctness-run times and Docker startup-inclusive times are not benchmark results.

## Rejected and deferred

- D lazy initialization: median 0.542397 -> 0.546646 seconds, no RSS gain; reverted.
- D worker counts 4/6/8/12: no robust K winner; part of screening overlapped validation, so retain 12. Raw screening retained as non-decisive evidence.
- Do not infer cache misses, branch mispredictions, memory bandwidth utilization, or hardware saturation from these stage timers.
- PGO and large graph preprocessing remain untested; there is no claim that all possible improvements are exhausted.

## Correctness and compliance

Exact hashes are in engineering/evidence/verification-*.json. Tiny, sample and full seed 1 plus sample seeds 42/99 are checked against references or official full-output hashes. Full fresh seed 20261031 is generated independently, then compared to the Python reference and repeated for determinism.

Targeted tests: empty A; 12 randomized directed graphs with all-pairs queries, zero/max weights and parallel/self edges; 70,000 queries on one D worker to exercise epoch wrap; 3,003 skewed/mixed Boolean and phrase queries; 350 names colliding into one C partition. ASan/UBSan and TSan passed tiny inputs on all tracks. These do not prove unseen-holdout correctness or exhaustive race freedom.

All four offline check.py Docker builds/runs passed full seed 1; every FROM allowlisted, images 30?31 MB, network disabled, read-only filesystem, unprivileged user, 8 GiB cap. Public repository, MIT license. All manifests specify the correct track/team and zero direct dependencies (D=1).

Official frozen source inspected at 3e365d15d9905a0d73c9f05207d434a654666d3c; remote main 7a9057bce3004c4120b01a480f51d8c0feaf74d7 adds only board run-history/tape publishing and tests, with no spec/generator/reference/harness changes.

## Limitations requiring external action

Board reports B registered=false even though B has successful scored runs and a correct local manifest. The flag comes from organizer team.tracks configuration; verify Track B registration with the organizer. No messages were sent on your behalf.

Top-five standing has not been achieved. Live rankings and timing vary. Historical best scores may come from older commits; final-tree measurements must be listed separately.

## Second-pass selection

B packs dictionary slots to 16 bytes and adds dynamic growth; final S=752. D replaces full-vertex worker arrays with exact sparse state, then reuses degree buffers as scatter cursors; final S=426. These supersede the first-pass table above. `engineering/evidence/b-compact.json`, `engineering/evidence/d-sparse.json`, `engineering/evidence/d-reuse.json` contain the isolated comparisons. Host timing changed substantially between windows; only paired measurements within a window support decisions. All 58 official generator/reference tests passed (they test the official implementation, not our binaries).

## Final selected local comparison

| Track | Baseline s | Selected s | Selected S |
|---|---:|---:|---:|
| A | 0.159341 | 0.156070 | 399 |
| B | 1.115314 | 0.867578 | 752 |
| C | 0.203503 | 0.168894 | 502 |
| D | 0.848750 | 0.735739 | 426 |

The late timing window is slower than the initial one; these matched pairs, not cross-window absolute times, are the release comparison. All final source paths passed full fresh seed 20261031.
