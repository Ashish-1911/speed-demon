# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

Our solution is implemented in modern C++20 with zero external dependencies.
- **Fast streaming ingestion**: Parses the fixed-grammar RFC 3339 timestamp prefix directly (`YYYY-MM-DDTHH:00:00Z`).
- **Delimiter scanning**: Tokenizes `method`, `endpoint`, `status`, and `latency` using single-byte scans without regular expressions.
- **Ordered grouping**: Aggregates records by `(hour, endpoint)` with an ordered mapping structure guaranteeing exact chronological and bytewise ascending order.
- **Nearest-rank percentiles**: Uses integer rank formula `(p * n + 99) / 100` for exact 1-indexed nearest-rank calculation.
- **Error PPM calculation**: Computes `floor(errors * 1,000,000 / count)` with 64-bit integer arithmetic.
- **Lightweight multi-stage container**: Compiled with pinned GCC 15 and deployed to minimal Debian 13 runtime image with static standard libraries.

## Dependencies

None (standard library only). `dependency_count = 0`.

## With another 24 hours

- Memory-mapped file I/O (`mmap`) with madvise sequential hints.
- SIMD whitespace / newline delimiter scanning.
- Cache-efficient arena storage and fast hash map with final lexicographical sort.
- Multi-threaded chunk processing across 12 hardware threads.
