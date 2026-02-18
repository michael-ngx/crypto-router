#pragma once

#include "venues/venue_factory.hpp"
#include "md/venue_feed.hpp"
#include "venues/coinbase/parser.hpp"
#include "venues/coinbase/ws.hpp"
#include "venues/coinbase/api.hpp"
#include "md/symbol_codec.hpp"

inline VenueFactory make_coinbase_factory() {
    VenueFactory factory;
    factory.name = "Coinbase";
    factory.make_feed = [](const std::string& canonical) -> std::shared_ptr<IVenueFeed> {
        using Feed = VenueFeed<CoinbaseWs, CoinbaseBookParser>;
        return std::make_shared<Feed>(
            "Coinbase", canonical, Backpressure::DropOldest, MAX_TOP_DEPTH);
    };
    factory.make_api = []() -> std::unique_ptr<IVenueApi> {
        return std::make_unique<CoinbaseVenueApi>();
    };
    factory.to_venue_symbol = [](const std::string& canonical) {
        return SymbolCodec::to_venue("Coinbase", canonical);
    };
    return factory;
}
