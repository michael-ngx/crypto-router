#pragma once

#include "venues/venue_api.hpp"

class KrakenVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Kraken"; }

    bool supports_pair(const std::string& canonical) const override {
        // TODO: Replace with Kraken REST asset pair check.
        (void)canonical;
        return true;
    }
};
