
Current lock behaviour:

- **Update threads (writers):** while a LevelCursor is alive, it holds a shared_lock, so writers (apply/apply_many with unique_lock) **wait**. They do not preempt router reads.
- **Read API calls (best_*, top_*):** they also take shared_lock, so they **can run concurrently** with routing cursors.
- <mark>**Tradeoff:** long routing reads can increase writer wait time (potential write starvation risk depends on shared_mutex fairness).</mark>