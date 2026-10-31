# Speed Demon Phase III Handoff

- Branch: `speed-demon`
- Checkpoint commit: `0c5f0e6` (refresh after this checkpoint is committed)
- Official scored source baseline: `ccdbb2a64b0b6335df8709a03c9d68834443fc8f`
- Rollback tag: `astro-scored-ccdbb2a`
- Experiment branch: `exp/phase2`

## Confirmed official runs

| Track | Run | T (s) | M (bytes) | S | K | Points |
|---|---|---:|---:|---:|---:|---:|
| A | `run-6aa7f879d0cd` | 0.106657 | 69038080 | 399 | 16.396307 | 60.737302 |
| B | `run-870bfa70b937` | 0.709844 | 251883520 | 752 | 10.012471 | 50.027063 |
| C | `run-44bf4f0a8ed0` | 0.130768 | 18784256 | 502 | 15.127926 | 58.988969 |
| D | `run-b50731622d5a` | 0.493443 | 253067264 | 426 | 11.072623 | 52.212525 |

Best-three geometric mean: `57.312932` machine points (A/C/D). Live board snapshot still reports rank 61 and fifth place `76.361508`.

## Phase III findings

- Track D `twopass_d.py` is complete and rejected: exact output, lower local RSS, but no reproducible end-to-end speedup. Evidence: `engineering/evidence/d-twopass.json`.
- Track D minimum-priority frontier expansion was exact but substantially slower and used much more memory; rejected.
- Track D cached radix-queue top candidate was locally positive but remains unintegrated pending complete validation and official scoring.
- Track C repeated-name/country cache was byte-exact with unchanged RSS but only a noisy ~1% local improvement; rejected.
- Track A 12-worker candidate was locally faster after eight alternating full-data runs, but its official score regressed; it is rejected and six workers are restored.
- Official A run `run-b68c2aba294b` for `1b99e94` passed all gates but regressed to 58.610548 points (`T=0.106818`, `M=112234496`, `K=14.866576`); six workers restored.
- Track D cached queue-top candidate was exact but only ~0.7% faster over eight alternating local runs with similar RSS; rejected.

## Outstanding

- Preserve unrelated untracked `DEMO-CV-017-unsigned-demo-passport.json` and `scratch/`.
- Docker checker is currently blocked because Docker is unavailable in WSL.
- Next: push the restored six-worker A baseline, then pursue only a materially stronger D or C candidate.
