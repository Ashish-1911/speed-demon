# Track C: Join

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/c-join.md).

## Approach

Our solution is written in modern C++20 with zero dependencies:
- **Zero-allocation streaming parser**: Robust RFC 4180 / hostile CSV state machine handling:
  - File-start and mid-file UTF-8 Byte Order Marks (`0xEF, 0xBB, 0xBF`) at record starts.
  - Escaped quotes (`""` -> `"`).
  - Embedded newlines (`\n`) and mixed CRLF/LF terminators.
  - Quoted fields with literal commas and spaces.
- **Join execution**: Region table loaded into an efficient hash lookup mapping `region_id -> country`, mapping unknown region IDs to `_UNMATCHED`.
- **Exact money parsing & formatting**: Parses money directly into signed 64-bit integer cents without floating-point arithmetic.
- **Accurate byte & distinct counting**: Maintains exact unescaped name byte strings in `std::unordered_set` for distinct counts and computes exact UTF-8 byte lengths for names and notes.
- **Total ordering**: Groups sorted lexicographically by country code with `_UNMATCHED` naturally placed last.

## Dependencies

None (standard library only). `dependency_count = 0`.

## With another 24 hours

- Dense vector lookup array indexed by `region_id` (measuring max region ID up to 10M).
- Custom string interner / hash set using Arena allocation to avoid dynamic allocations for names.
- Multi-threaded parallel chunk scanning with record alignment detection.
