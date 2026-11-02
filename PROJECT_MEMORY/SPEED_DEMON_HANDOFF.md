# Speed Demon architecture handoff — November 2

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Production baseline post-recovery: Restored Track A (`f153bef`), restored Track C (`f153bef`), preserved Track D (`77b5f88` bucketed CSR).
- Existing rollback tags and references preserved. Untracked `scratch/` and passport JSON preserved.
- Official snapshot 2026-11-02T05:15:27Z: rank 90; top 5 threshold 82.760378; A/C/D best portfolio 59.887411.

| Track | Best official run | Commit | T seconds | M bytes | S | K | Points | Status |
|---|---|---|---:|---:|---:|---:|---:|---|
| A | run-9432271f699b | 03d213d | 0.092656 | 62230528 | 399 | 17.960606 | 62.716049 | NEW PERSONAL BEST (Chained Forward-Star Arena) |
| C | run-56b04dbf00b6 | 03d213d | 0.120381 | 16179200 | 502 | 16.244956 | 60.535927 | NEW PERSONAL BEST (Separated Metadata Compact Dict) |
| D | run-de64b2001951 | 5febbbf | 0.351957 | 217686016 | 421 | 13.535503 | 56.573720 | NEW PERSONAL BEST (Bucketed CSR) |

- Portfolio: (62.716049 × 60.535927 × 56.573720)^(1/3) = 59.887411.
- Top 5 leaderboard:
  1. another ello: 84.948491
  2. ALL STAR: 84.637955
  3. SALAAR: 84.126389
  4. Cause Of Death: 83.305269
  5. NickTrapp: 82.760378

Next executable tasks:
1. Preserve verified winning implementations across all three tracks:
   - Track A: Chained Forward-Star Arena (commit 03d213d, run-9432271f699b, 62.716049 pts)
   - Track C: Separated Metadata Compact Dictionary (commit 03d213d, run-56b04dbf00b6, 60.535927 pts)
   - Track D: Bucketed CSR (commit 5febbbf/03d213d, run-de64b2001951, 56.573720 pts)
2. Perform comprehensive holdout seed validation and offline verification checks before freeze.
3. Commit clean performance logs and documentation ahead of code freeze (15:00 UTC / 20:30 IST).
