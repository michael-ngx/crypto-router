#pragma once
#include <memory>
#include <vector>
#include <string>
#include <utility>
#include <cstdint>
#include <mutex>

#include "md/venue_feed_iface.hpp"

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

    // Exchanges currently contributing live, non-empty levels.
    std::vector<std::string> venues;

    // True when all venues are stale or missing.
    bool is_cold{false};

    // True while feeds are initializing and no transport has been observed yet.
    bool is_warming{false};

    // True when transport is live but no recent book updates were observed.
    bool is_quiet{false};

    // Latest update time across venues with a published snapshot (epoch ms).
    std::int64_t last_updated_ms{0};
};

// UIMasterFeed collects IVenueFeed readers and builds a consolidated ladder
// by merging top levels from immutable per-venue BookSnapshot objects.
// Thread-safe for add/get.
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
