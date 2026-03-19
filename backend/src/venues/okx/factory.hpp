#pragma once

#include "venues/venue_factory.hpp"
#include "md/venue_feed.hpp"
#include "venues/okx/parser.hpp"
#include "venues/okx/ws.hpp"
#include "venues/okx/api.hpp"
#include "md/symbol_codec.hpp"

inline VenueFactory make_okx_factory() {
    VenueFactory factory;
    factory.name = "OKX";
    factory.make_feed = [](const std::string& canonical) -> std::shared_ptr<IVenueFeed> {
        using Feed = VenueFeed<OkxWs, OkxBookParser>;
        return std::make_shared<Feed>(
            "OKX", canonical, Backpressure::DropOldest);
    };
    factory.make_api = []() -> std::unique_ptr<IVenueApi> {
        return std::make_unique<OkxVenueApi>();
    };
    factory.to_venue_symbol = [](const std::string& canonical) {
        return SymbolCodec::to_venue("OKX", canonical);
    };
    return factory;
}
