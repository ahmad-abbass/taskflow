# Core API benchmark

Run `make benchmark`. The executable reports CSV rows with operation count, elapsed wall time, and a checksum that keeps the work observable. It measures the C++ core API; CLI parsing, snapshot history, and process startup are excluded.

One run on this Linux workspace, GCC 13.3, `-std=c++17 -O2`, October 19, 2026:

| Operation | Stored tasks | Operations | Milliseconds | Checksum |
| --- | ---: | ---: | ---: | ---: |
| Submit with hash and AVL indexes | 20,000 | 20,000 | 18.348 | 20,000 |
| Hash lookup | 20,000 | 5,000 | 0.371 | 47,214 |
| Linear lookup reference | 20,000 | 5,000 | 108.219 | 47,214 |
| AVL rank followed by select | 20,000 | 5,000 | 3.024 | 47,214 |
| Four-slot simulation | 20,000 | 20,000 | 4.341 | 20,000 |

Lookup cases use the same fixed-seed query sequence and produce the same checksum. The linear reference scans an insertion-order vector; it is a baseline for the earlier registry design, not a competing library hash implementation. Rank/select does more work than direct lookup and serves ordered queries.

These are single-run observations, not statistical performance claims. Host load, compiler, cache state, keys, and workload affect timings. A four-slot simulation remains synchronous code; its timing is not evidence of multithreaded speedup or real job-processing throughput. Re-run on the target machine before drawing a performance conclusion.
