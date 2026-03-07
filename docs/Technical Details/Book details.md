We face an important trade off regarding the implementation of market data, and router

Each combination of `venue-pair` corresponds to a feed (book) in our platform. The feed is constantly updated by messages sent from exchanges.

- There are multiple readers (scale to many many users)
- Few writers (only 1 for each book)
- Reads must be extremely fast (for UI updates/routing calculation)

This leads us to implement the <mark>RCU Immutable View</mark>

### For single users (deprecated)

Fastest routing implementation is to implement `LevelCursor`, allowing iteration across all venues of the same pair at the same time, minimizing cost.

Current lock behaviour:

- **Update threads (writers):** while `a LevelCursor is` alive, it holds a shared_lock, so writers (apply/apply_many with unique_lock) **wait**. They do not preempt router reads.

However, this is not scalable. Feed data needs to be constantly updated, and `LevelCursor` implementation requires stale book data. If many users try to route a trading pair at the same time... (many reads) data updates are stalled (writes)


### To support a scaled amount of users

We proceed with a different approach. Book updates are write-only. Always update whenever new packets arrive from WebSocket, non-locking.

Frequently, full-depth snapshots are "published". Publish policy in `VenueFeed`:
- min_publish_interval_ns (default 500000 = 0.5ms)
- max_updates_per_publish (default 32)
- top_size_rel_change_trigger (default 0.05)

Subsequently, routers fetch the books snapshots and quickly calculate routing path. *The rest is history...*