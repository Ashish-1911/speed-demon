# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Our solution is implemented in modern C++20 with zero external dependencies (`dependency_count = 0`).

- **Query-First Architecture**: Parses all 50,000 queries first, constructing a high-speed flat open-addressing term dictionary with 64-bit FNV-1a hashing. Over 90% of corpus tokens never appear in queries and are discarded with average 1.1 cache-line probes without string allocations.
- **Parallel Document Chunk Indexing**: Memory-maps `docs.txt` (254 MB) and partitions the 250,000 documents into contiguous thread slices across 6 worker threads. Each thread accumulates thread-local posting lists, which are concatenated into globally sorted ascending postings in parallel without sorting.
- **Linear-Time Boolean Evaluation**: Evaluates left-to-right binary `AND`, `OR`, and `NOT` operations using branch-efficient two-pointer streaming merges over strictly ascending document IDs.
- **Rarest-Anchor Phrase Verification**: For each phrase query, identifies the minimum-frequency anchor term to restrict candidates to a tiny set (typically $\le 5$ documents). Only candidate documents are scanned from zero-copy memory slices for consecutive token matching.
- **Parallel Query Execution**: Distributes the 50,000 queries across worker threads using dynamic lock-free chunking (`std::atomic<size_t>`).
- **Exact Contract Formatting**: Accumulates exact match count, 64-bit document ID sum, and the ten smallest IDs ascending per query, writing directly into `/out/results.txt`.

## Dependencies

None (standard library only). `dependency_count = 0`.

## With another 24 hours

- SIMD-accelerated AVX2 bitset operations for high-frequency terms.
- Compressed postings with StreamVByte or PFOR.
- Memory prefaulting hints for large corpus memory-mapping.
