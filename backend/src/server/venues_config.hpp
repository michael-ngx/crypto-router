#pragma once

#include <string>
#include <vector>

struct VenueConfig {
    std::string name;
};

inline const std::vector<VenueConfig> kVenueConfigs = {
    {"Binance"},
    {"Coinbase"},
    {"Kraken"},
    {"OKX"},
};
