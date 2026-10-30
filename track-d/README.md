# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

Our solution is written in modern C++20 with zero dependencies:
- **Fast POSIX buffered integer parser**: Fast chunked I/O (`FastReader`) directly tokenizing decimal integers without stream overhead.
- **Compact CSR graph**:
  - Two-pass CSR index generation (degree counting prefix sums) mapping directed edges into contiguous flat vectors.
  - Zero dynamic heap reallocations per vertex.
- **Epoch-indexed Dijkstra search**:
  - Uses an epoch generation counter on `seen_epoch` and `dist` arrays, avoiding clearing large 1.7M-element distance vectors between queries.
  - O(1) early check for `s == t` returning 0.
  - 64-bit distances with priority queue rejecting stale entries.
  - Terminates immediately once target node `t` is settled.

## Dependencies

None (standard library only). `dependency_count = 0`.

## With another 24 hours

- Bidirectional Dijkstra search from source and target with balanced stopping criterion.
- Parallel query processing across the 12 available hardware threads.
- SIMD integer parsing and memory-mapped file reader (`mmap`).
- 4-ary / d-ary heap or radix heap for cache-friendly priority queue operations.
