# Speed Demon architecture handoff — November 2

- Scope: A/C/D only; B is unchanged and unregistered. Do not register or optimize it.
- Production baseline post-recovery: Restored Track A (`f153bef`), restored Track C (`f153bef`), preserved Track D (`77b5f88` bucketed CSR).
- Existing rollback tags and references preserved. Untracked `scratch/` and passport JSON preserved.
- Official snapshot 2026-11-02T04:27:28Z: rank 90; top 5 threshold 82.562243; A/C/D best portfolio 57.995110.

| Track | Best official run | Commit | T seconds | M bytes | S | K | Points | Status |
|---|---|---|---:|---:|---:|---:|---:|---|
| A | run-4a61d3b39439 | f153bef | 0.102003 | 68935680 | 399 | 16.771163 | 61.228159 | Restored (77b5f88 regressed: 60.505163) |
| C | run-44bf4f0a8ed0 | ccdbb2a/f153bef | 0.130768 | 18784256 | 502 | 15.127926 | 58.988969 | Restored (77b5f88 regressed: 58.764891) |
| D | run-00991e107376 | 77b5f88 | 0.445644 | 217886720 | 421 | 12.026663 | 54.007256 | Preserved winning Astra bucketed CSR |

- Portfolio: (61.228159 × 58.988969 × 54.007256)^(1/3) = 57.995110.
- Top 5 leaderboard:
  1. another ello: 84.948491
  2. ALL STAR: 84.637955
  3. SALAAR: 84.019250
  4. Cause Of Death: 83.305269
  5. LippInc: 82.562243
  Gap to 5th place: 24.567 points.

Next executable tasks:
1. Submitting Track C breakthrough: Compact Dictionary with Separated Metadata (T -6.7%, M -4.8MB, S 505, projected points 61.03).
2. Retrieve official run ID and scored SHA for Track C; verify gates and score on liveboard.
3. Keep or revert Track C based on official K.
4. Next candidate: Track D compact state query optimization (T -4.5%, S 435, projected points 54.93).
5. Freeze: November 2, 2026 — 15:00 UTC / 20:30 IST.
