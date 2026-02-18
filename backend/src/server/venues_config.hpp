#pragma once

#include <string>
#include <vector>

struct VenueConfig {
    std::string name;
};

inline const std::vector<VenueConfig> kVenueConfigs = {
    {"Coinbase"},
    {"Kraken"},
};
