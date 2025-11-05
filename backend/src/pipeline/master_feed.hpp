#pragma once
#include <memory>
#include <vector>
#include <string>
#include <utility>
#include <unordered_map>
#include <mutex>

#include "venue_feed_iface.hpp"
#include "top_snapshot.hpp"

// A unified consolidated top-of-book view for the UI.
struct UIConsolidated {
    std::string symbol; // canonical, e.g., "BTC-USD"
    // Aggregated ladders across all venues (sum sizes at identical prices).
    std::vector<std::pair<double,double>> bids; // best->worse
    std::vector<std::pair<double,double>> asks; // best->worse

    // Per-venue snapshots for side panels or debugging
    std::unordered_map<std::string, std::shared_ptr<const TopSnapshot>> per_venue;
};

// UIMasterFeed collects IVenueFeed readers and builds a consolidated ladder
// by k-way merging their published TopSnapshot objects. Thread-safe for add/get.
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