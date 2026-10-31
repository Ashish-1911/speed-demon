# Speed Demon Phase II Handoff

- Branch: `speed-demon`
- Checkpoint commit: `c506efe` (refresh this line only when checkpoint content changes)
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

## Phase II findings

- Track D `twopass_d.py` is complete and rejected: exact output, lower local RSS, but no reproducible end-to-end speedup. Evidence: `engineering/evidence/d-twopass.json`.
- Latest stage profile: A parsing dominates; B tokenization/index construction dominates; C partition aggregation dominates; D graph parse and query work are the main costs.
- Track B chunked hash candidate: exact output, slower (`~1.198s` to `~1.322s` median); rejected.
- Track B exact query-prefix filter candidate: exact output, slower (`~1.187s` to `~1.269s` median); rejected.
- No Phase II source candidate is integrated.

## Outstanding

- Board metadata still reports Track B `registered: false`. Registration is controlled by the official submission form, not `.speed-demon.toml`; account owner should verify that Track B was selected for this repository/team.
- Preserve unrelated untracked `DEMO-CV-017-unsigned-demo-passport.json` and `scratch/`.
- Next command: `git status --short --branch; git diff --check; python3 check.py a --data data/a_full --expected 495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3`
