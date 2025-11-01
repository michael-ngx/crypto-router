/*
Normalized tick data structure
*/

#pragma once
#include <string>
#include <cstdint>

struct NormalizedTick
{
    std::string venue;  // "coinbase", "kraken"
    std::string symbol; // canonical "BTC-USD"
    double bid{0}, ask{0}, last{0};
    std::int64_t ts_ns{0}; // exchange or recv timestamp
};