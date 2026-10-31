# Speed Demon Phase III Handoff

- Branch: `speed-demon`
- Checkpoint commit: `d2aba07` (refresh after this checkpoint is committed)
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

Best-three A/C/D geometric mean: `57.191787` machine points. Live board snapshot previously reported rank 61 and fifth place `76.361508`.

The 2026-10-31T17:30:58Z board snapshot confirms the same A/C/D best runs, rank 61, and a current fifth-place portfolio of `76.421636`; the current A/C/D geometric mean is `57.191787`, leaving a `19.229849` gap.

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
- Windows Docker CLI is present but the Docker Desktop Linux engine is not running; WSL exposes the CLI path but no usable daemon.
- Restored six-worker A baseline is published as `ba07f44`; no production source change remains pending.
- No materially stronger D or C candidate was found in this pass; the queue-top D cache and repeated-name C cache remain rejected.
- Phase IV source-order D scheduling was exact but only ~1–2% locally faster; rejected.
- Phase IV C lock-free existing-slot update path was slower (median ~2.84 s vs ~2.75 s); rejected.
- No production Track A/C/D source changes are pending. Docker remains blocked because the Windows CLI has no running Linux engine.
