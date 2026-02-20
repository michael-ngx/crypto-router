
1) Market data ingestion continues in parallel; only <mark>HTTP request handlers are serialized</mark> on one IO thread.

If you want low order latency under load, the next step is to parallelize HTTP handling (multi-thread ioc.run() and/or offload heavy order routing+DB work to a worker pool).

NOTE: If multiple users access the website at a time, then all of them are calling `/api/book/`, which is **BLOCKING each other! There's a high need for implementing concurrent shi here.*


2) Make router and execution async. This requires: we should extend guard ownership to the async job lifecycle (e.g., per-order ref in manager) so the pair stays alive until final terminal status.


3) Add endpoint/test for `order_breakdown` retrieval so post-trade UI can show venue split details


