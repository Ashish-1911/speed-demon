# Speed Demon architecture handoff — November 2

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Production baseline post-recovery: Restored Track A (`f153bef`), restored Track C (`f153bef`), preserved Track D (`77b5f88` bucketed CSR).
- Existing rollback tags and references preserved. Untracked `scratch/` and passport JSON preserved.
- Official snapshot 2026-11-02T05:05:51Z: rank 90; top 5 threshold 82.760378; A/C/D best portfolio 59.406807.

| Track | Best official run | Commit | T seconds | M bytes | S | K | Points | Status |
|---|---|---|---:|---:|---:|---:|---:|---|
| A | run-4a61d3b39439 | f153bef | 0.102003 | 68935680 | 399 | 16.771163 | 61.228159 | Restored production baseline |
| C | run-af62c4baaf8a | aa0b46e | 0.120649 | 16125952 | 502 | 16.237605 | 60.526099 | NEW PERSONAL BEST (Separated Metadata Compact Dict) |
| D | run-de64b2001951 | 5febbbf | 0.351957 | 217686016 | 421 | 13.535503 | 56.573720 | NEW PERSONAL BEST (Bucketed CSR) |

- Portfolio: (61.228159 × 60.526099 × 56.573720)^(1/3) = 59.406807.
- Top 5 leaderboard:
  1. another ello: 84.948491
  2. ALL STAR: 84.637955
  3. SALAAR: 84.123171
  4. Cause Of Death: 83.305269
  5. NickTrapp: 82.760378

Next executable tasks:
1. Submitting Track A breakthrough: Chained Forward-Star Arena with In-Place Reduction (-15.7 MB RAM, T -4.3%, S preserved at 399, all 8 suites pass 100% bit-for-bit).
2. Restoring Track D main.cpp to PB commit 5febbbf (S 421).
3. Retrieve official run ID and score on liveboard; keep/revert.
4. Final verification against holdout seeds before code freeze (15:00 UTC / 20:30 IST).
