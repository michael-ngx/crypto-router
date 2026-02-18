#include "../src/md/venue_feed.hpp"
#include "../src/ui/master_feed.hpp"
#include "../src/venues/coinbase/ws.hpp"
#include "../src/venues/kraken/ws.hpp"
#include "../src/venues/coinbase/parser.hpp"
#include "../src/venues/kraken/parser.hpp"
#include "../src/md/symbol_codec.hpp"

#include <iostream>
#include <thread>
#include <memory>

int main() {
    const std::string canonical = "BTC-USD";

    // Venue feeds
    using CbFeed = VenueFeed<CoinbaseWs, CoinbaseBookParser>;
    using KrFeed = VenueFeed<KrakenWs,  KrakenBookParser >;

    auto cb = std::make_shared<CbFeed>("Coinbase", canonical, Backpressure::DropOldest, /*top_depth*/10);
    auto kr = std::make_shared<KrFeed>("Kraken",  canonical, Backpressure::DropOldest, /*top_depth*/10);

    // Start WS
    cb->start_ws(SymbolCodec::to_venue("Coinbase", canonical), 443);
    kr->start_ws(SymbolCodec::to_venue("Kraken",  canonical), 443);

    // Master UI
    UIMasterFeed ui(canonical);
    ui.add_feed(std::static_pointer_cast<IVenueFeed>(cb));
    ui.add_feed(std::static_pointer_cast<IVenueFeed>(kr));

    // Let books warm up
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Pull a consolidated ladder
    auto cons = ui.snapshot_consolidated(/*depth*/10);

    std::cout << "=== CONSOLIDATED " << cons.symbol << " ===\n";
    std::cout << "BIDS:\n";
    for (auto& [px, sz] : cons.bids) {
        std::cout << "  " << px << "  " << sz << "\n";
    }
    std::cout << "ASKS:\n";
    for (auto& [px, sz] : cons.asks) {
        std::cout << "  " << px << "  " << sz << "\n";
    }

    // Optional: per-venue preview
    for (auto& kv : cons.per_venue) {
        auto sp = kv.second;
        if (!sp) continue;
        std::cout << "\n[" << sp->venue << "] top " << sp->bids.size() << "/" << sp->asks.size() << "\n";
        if (!sp->bids.empty())
            std::cout << "  best bid: " << sp->bids.front().first << " x " << sp->bids.front().second << "\n";
        if (!sp->asks.empty())
            std::cout << "  best ask: " << sp->asks.front().first << " x " << sp->asks.front().second << "\n";
    }

    // Run a bit longer then stop
    std::this_thread::sleep_for(std::chrono::seconds(10));
    cb->stop();
    kr->stop();
    return 0;
}

/*
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
SIMDJSON_PREFIX=$(brew --prefix simdjson)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/venues/coinbase/ws.cpp src/venues/kraken/ws.cpp \
  src/md/symbol_codec.cpp \
  src/ui/master_feed.cpp \
  test/test_master.cpp \
  -I src -I"$SIMDJSON_PREFIX/include" -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  -L"$SIMDJSON_PREFIX/lib" -lsimdjson \
  -L"$OPENSSL_PREFIX/lib" -lssl -lcrypto \
  -Wl,-rpath,"$SIMDJSON_PREFIX/lib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_master

./build/test_master
*/
