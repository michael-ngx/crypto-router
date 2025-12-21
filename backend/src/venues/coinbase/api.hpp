#pragma once

#include "venues/venue_api.hpp"

class CoinbaseVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Coinbase"; }

    bool supports_pair(const std::string& canonical) const override {
        // TODO: Replace with Coinbase REST product listing check.
        (void)canonical;
        return true;
    }
};
