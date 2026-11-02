# Speed Demon architecture handoff — November 2

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Production baseline post-recovery: Restored Track A (`f153bef`), restored Track C (`f153bef`), preserved Track D (`77b5f88` bucketed CSR).
- Existing rollback tags and references preserved. Untracked `scratch/` and passport JSON preserved.
- Official snapshot 2026-11-02T04:58:16Z: rank 89; top 5 threshold 82.760378; A/C/D best portfolio 59.102379.

| Track | Best official run | Commit | T seconds | M bytes | S | K | Points | Status |
|---|---|---|---:|---:|---:|---:|---:|---|
| A | run-4a61d3b39439 | f153bef | 0.102003 | 68935680 | 399 | 16.771163 | 61.228159 | Restored production baseline |
| C | run-9103d8323bf4 | a63163e | 0.131682 | 16035840 | 502 | 15.559920 | 59.600369 | NEW PERSONAL BEST (Separated Metadata Compact Dict) |
| D | run-de64b2001951 | 5febbbf | 0.351957 | 217686016 | 421 | 13.535503 | 56.573720 | NEW PERSONAL BEST (Bucketed CSR) |

- Portfolio: (61.228159 × 59.600369 × 56.573720)^(1/3) = 59.102379.
- Top 5 leaderboard:
  1. another ello: 84.948491
  2. ALL STAR: 84.637955
  3. SALAAR: 84.027665
  4. Cause Of Death: 83.305269
  5. NickTrapp: 82.760378

Next executable tasks:
1. Pushing Track D optimization: Unified Templated State Relaxation & Compact Hash Layout (T -7.44%, S 409, all 9 test suites pass 100% bit-for-bit).
2. Retrieve official run ID and score on liveboard; keep/revert.
3. Investigate Track A parsing & memory layout optimization to lower memory footprint and runtime.
4. Freeze: November 2, 2026 — 15:00 UTC / 20:30 IST.
