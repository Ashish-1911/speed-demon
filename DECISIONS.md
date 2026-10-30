# Speed Demon 2026 — Engineering Decisions & Operational Log

## Competition Identity & Scope
- **Repository Branch**: `speed-demon`
- **Compiler Image**: `gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce`
- **Runtime Image**: `debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad`
- **Language**: C++20 with zero third-party dependencies (`dependency_count = 0`).

---

## Decisions & Milestones

### Decision 01: Multi-stage Docker Build for C++20
- **Context**: Solutions must compile offline (`--network=none`), fit within 2 GB image size, and use allowlisted images pinned by multi-arch digest.
- **Decision**: Multi-stage build with `gcc:15` compiling with `-std=c++20 -O3 -static-libstdc++ -static-libgcc`, copying only the single compiled binary into `debian:13-slim`.
- **Result**: Track A Docker image is only ~30 MB (far below the 2 GB cap), builds in ~1.2s, and has zero runtime dependencies.

### Decision 02: Track A Baseline Implementation
- **Implementation**:
  - Direct 20-character hour string truncation from fixed 27-byte RFC 3339 timestamp.
  - Linear scan delimiter parsing for method, endpoint, status, and latency without regex.
  - `std::map<std::pair<std::string, std::string>, GroupData>` providing automatic total chronological and bytewise ascending ordering.
  - Exact 1-indexed nearest-rank percentile formula `(p * n + 99) / 100` and integer floor error ppm calculation.
- **Verification**:
  - Tiny dataset: SHA-256 `c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f` (exact match).
  - Sample dataset: SHA-256 `ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3` (exact match).
  - Both verified with official `check.py` under full offline sandbox restrictions.
