#pragma once

#include "venues/venue_factory.hpp"
#include "md/venue_feed.hpp"
#include "venues/binance/parser.hpp"
#include "venues/binance/ws.hpp"
#include "venues/binance/api.hpp"
#include "md/symbol_codec.hpp"

inline VenueFactory make_binance_factory() {
    VenueFactory factory;
    factory.name = "Binance";
    factory.make_feed = [](const std::string& canonical) -> std::shared_ptr<IVenueFeed> {
        using Feed = VenueFeed<BinanceWs, BinanceBookParser>;
        return std::make_shared<Feed>(
            "Binance", canonical, Backpressure::DropOldest);
    };
    factory.make_api = []() -> std::unique_ptr<IVenueApi> {
        return std::make_unique<BinanceVenueApi>();
    };
    factory.to_venue_symbol = [](const std::string& canonical) {
        return SymbolCodec::to_venue("Binance", canonical);
    };
    return factory;
}
