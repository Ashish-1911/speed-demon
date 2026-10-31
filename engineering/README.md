# Reproducing the campaign

Run the Python scripts under WSL with GCC 15.2. They use only the Python standard
library and compile the submitted C++20 code. `campaign.py` defines the repository,
original data/reference checkout, and `/tmp/sd-campaign` paths. Adjust `BASE` for a
different machine. Copy the original `data/` directory into
`/tmp/sd-campaign/data/` before running; use Linux-native storage for comparisons.
The original frozen official repository must be available at `BASE/official`.

`campaign.py profile` compiles baseline and instrumented current implementations.
The baseline compiler always reads immutable commit `a3ab590`, not the current
submission branch. `campaign.py compare TRACK NAME BASE_NAME` compiles the current
track as NAME and runs seven alternating comparisons. `compare-built` uses
already compiled candidates. Never run benchmarks concurrently with other work.

`validate.py TRACK BINARY_NAME` checks the existing five datasets. `validate.py
fresh` generates full seed 20261031 and independently runs the Python references.
`adversarial.py` checks targeted cases, including dictionary saturation and graph
epoch workload. `sanitize.py` builds and runs tiny cases under ASan/UBSan and TSan.
`d_structures.cpp` separately exercises true 32-bit epoch wrap, sparse table growth,
and the full 64-bit radix range; compile with the same sanitizers and run it.

`get_static_tools.py` downloads and SHA-256 verifies the organizer-pinned tools.
`static_count.py` formats copies, counts C++ and Docker code, and reproduces all
four baseline S values. It is not the entire organizer sandbox harness.

`variants/` preserves isolated experiment sources outside scored track folders.
The compact/sparse/reuse scripts reconstruct their respective candidate from the
preceding saved source. Raw paired timings, output hashes, and sanitizer results
are in `evidence/`. These include intermediate candidates; consult CAMPAIGN.md
and each track's PERF.md to identify the selected version. GNU RSS includes file
mapping pages and is not interchangeable with the board's memory measurement.

The original user workspace's `scratch/` and passport file were left untouched.
