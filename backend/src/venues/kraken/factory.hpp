#pragma once

#include "venues/venue_factory.hpp"
#include "md/venue_feed.hpp"
#include "venues/kraken/parser.hpp"
#include "venues/kraken/ws.hpp"
#include "venues/kraken/api.hpp"
#include "md/symbol_codec.hpp"

inline VenueFactory make_kraken_factory() {
    VenueFactory factory;
    factory.name = "Kraken";
    factory.make_feed = [](const std::string& canonical) -> std::shared_ptr<IVenueFeed> {
        using Feed = VenueFeed<KrakenWs, KrakenBookParser>;
        return std::make_shared<Feed>(
            "Kraken", canonical, Backpressure::DropOldest, MAX_TOP_DEPTH);
    };
    factory.make_api = []() -> std::unique_ptr<IVenueApi> {
        return std::make_unique<KrakenVenueApi>();
    };
    factory.to_venue_symbol = [](const std::string& canonical) {
        return SymbolCodec::to_venue("Kraken", canonical);
    };
    return factory;
}
