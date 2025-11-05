#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

// Immutable, per-venue Top-N view captured at a point in time.
// Carried around via shared_ptr<const TopSnapshot>.
struct TopSnapshot {
    std::string venue;   // "coinbase", "kraken", ...
    std::string symbol;  // canonical, e.g. "BTC-USD"
    std::int64_t ts_ns{0};

    // best-to-worse order
    std::vector<std::pair<double,double>> bids; // (price, size)
    std::vector<std::pair<double,double>> asks; // (price, size)
};