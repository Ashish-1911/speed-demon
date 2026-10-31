# Track D: Architecture & Decision Record

## Decision Record 01: Single-Pass Parallel CSR Construction

- **Date**: 2026-10-31
- **Status**: Accepted
- **Context**:
  Track D's baseline implementation used a two-pass parser over `graph.txt`:
  1. Pass 1 scanned the 88 MB file to count per-worker node degrees into `worker_counts_f` and `worker_counts_r`.
  2. Single-threaded prefix sums and column-major offset generation populated `worker_offset_f` and `worker_offset_r` across 12 threads ($12 \times 1,740,512 \times 4$ bytes each = 168 MB).
  3. Pass 2 re-parsed the 88 MB file from disk/mmap to populate CSR arrays.
  Profiling revealed that this build process consumed 373.78 ms (48.8% of total process runtime), with 136.40 ms spent in the column-major prefix sum loop alone due to 20 million cache misses.
- **Decision**:
  Implement a single-pass parallel CSR construction algorithm:
  - Parse `(u, v, w)` once in parallel, buffering raw edges in per-worker vectors and concurrently counting node degrees.
  - Compute degree prefix sums in parallel across node segments.
  - Eliminate `worker_offset_f` and `worker_offset_r` global matrices; each worker thread computes its own linear write offsets locally.
  - Scatter edges into `edges` and `r_edges` concurrently and free all temporary build buffers prior to query execution.
- **Consequences**:
  - Eliminates 336 MB of temporary matrices from the heap.
  - Drops graph construction time by 118 ms (from 373.8 ms to 255.4 ms).
  - Shaves total process runtime from 766 ms to 549 ms on full scale.
  - Preserves exact bit-for-bit output across all seeds and edge cases (Tiny, Sample, Full, Seed 42, Seed 99, Fresh Seed 777).
