# Speed Demon architecture handoff — November 2

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Final locked production baseline:
  - Track A: Chained Forward-Star Arena with In-Place Reduction (`03d213d` / `a8b5bd2` / `fb8b6c4`, run `run-9432271f699b`)
  - Track C: Parallel Quote-Aware Chunk Discovery + Compact Dictionary (`fb8b6c4`, run `run-8de06b07b87c`)
  - Track D: Bucketed CSR + Radix Heap Bidirectional Dijkstra (`5febbbf` / `03d213d` / `fb8b6c4`, run `run-de64b2001951`)
- Final Release Tag: `release-final-pb-60.387`
- Official snapshot 2026-11-02T14:43:02Z: rank #84 / 144; A/C/D personal best portfolio: **60.387349**.

| Track | Best official run | Commit | T seconds | M bytes | S | K | Points | Status |
|---|---|---|---:|---:|---:|---:|---:|---|
| A | run-9432271f699b | 03d213d | 0.092656 | 62230528 | 399 | 17.960606 | 62.716049 | WINNING BEST (Chained Forward-Star Arena) |
| C | run-8de06b07b87c | fb8b6c4 | 0.102158 | 16216064 | 541 | 17.429688 | 62.064480 | WINNING BEST (Parallel Chunk Discovery) |
| D | run-de64b2001951 | 5febbbf | 0.351957 | 217686016 | 421 | 13.535503 | 56.573720 | WINNING BEST (Bucketed CSR) |

- Machine Portfolio: (62.716049 × 62.064480 × 56.573720)^(1/3) = 60.387349.
- Leaderboard Rank: #84 / 144.

Verification & Compliance:
1. 24 of 24 datasets pass with 100% bit-exact SHA-256 matches across tiny, sample, full, holdout seeds 42 & 99, fresh 20261101, and targeted adversarial edge cases.
2. Sanitizers (ASan, UBSan, TSan) report 0 errors, 0 leaks, 0 races across all tracks.
3. GCC 15 release builds compile cleanly with zero warnings under `-O3 -flto -march=haswell -pthread`.
4. Track B is strictly untouched and unregistered.
5. All PERF.md files accurately record every official run ID and architectural justification for the 35% human judging evaluation.
