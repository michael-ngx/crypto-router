#pragma once
#include <memory>
#include <vector>
#include <string>
#include <utility>
#include <unordered_map>
#include <mutex>

#include "md/venue_feed_iface.hpp"
#include "md/top_snapshot.hpp"

// One row in the UI ladder with venue information.
struct UILadderLevel {
    std::string venue;
    double price{0};
    double size{0};
};

// A unified consolidated view for the UI.
struct UIConsolidated {
    std::string symbol; // canonical, e.g., "BTC-USD"

    // Consolidated ladders across all venues.
    // Bids: highest price first; Asks: lowest price first.
    std::vector<UILadderLevel> bids;
    std::vector<UILadderLevel> asks;

    // Per-venue snapshots for side panels or debugging.
    std::unordered_map<std::string, std::shared_ptr<const TopSnapshot>> per_venue;
};

// UIMasterFeed collects IVenueFeed readers and builds a consolidated ladder
// by merging their TopSnapshot objects. Thread-safe for add/get.
class UIMasterFeed {
public:
    explicit UIMasterFeed(std::string canonical_symbol)
        : canonical_(std::move(canonical_symbol)) {}

    // Register a venue feed (must match symbol).
    void add_feed(std::shared_ptr<IVenueFeed> feed);

    // Build a consolidated ladder of depth N for both sides.
    // Reads each venue's snapshot atomically (lock-free from venues’ perspective).
    UIConsolidated snapshot_consolidated(std::size_t depth) const;

private:
    std::string canonical_;
    mutable std::mutex m_; // protects feeds_
    std::vector<std::shared_ptr<IVenueFeed>> feeds_;
};
