#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Precomputed per-level book features in best-to-worse order.
// Shared by routing and UI consumers.
struct BookSnapshotLevel {
    double price{0.0};
    double size{0.0};
    double cum_qty{0.0};
    double cum_notional{0.0};
};

// Immutable full-depth per-venue book snapshot.
// Shared by routing and UI readers.
// Published atomically as shared_ptr<const BookSnapshot>.
struct BookSnapshot {
    std::string venue;
    std::string symbol;
    std::uint64_t seq{0}; // monotonic local publish sequence
    std::int64_t ts_ns{0};
    std::int64_t ts_ms{0};

    std::vector<BookSnapshotLevel> bids;
    std::vector<BookSnapshotLevel> asks;
};
