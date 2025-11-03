#include <chrono>
#include <iostream>
#include <iomanip>
#include <thread>

#include "pipeline/venue_feed.hpp"
#include "md/symbol_codec.hpp"
#include "md/book.hpp"
#include "md/book_parser_coinbase.hpp"

// Alias the feed for Coinbase
using CbFeed = VenueFeed<CoinbaseWs, CoinbaseBookParser, 4096>;

static void print_price_size(double px, double sz) {
    std::cout << std::fixed << std::setprecision(2) << px
              << " x "
              << std::setprecision(8) << sz
              << std::defaultfloat; // restore default if you want
}

static void print_snapshot(const Book& b, std::size_t n = 10) {
    auto bb = b.best_bid();
    auto ba = b.best_ask();

    std::cout << "\n[summary] venue=" << b.venue()
              << " symbol=" << b.symbol()
              << " bid_levels=" << b.bid_levels()
              << " ask_levels=" << b.ask_levels() << "\n";

    if (bb) { std::cout << "  best_bid: "; print_price_size(bb->first, bb->second); std::cout << "\n"; }
    if (ba) { std::cout << "  best_ask: "; print_price_size(ba->first, ba->second); std::cout << "\n"; }

    auto bids = b.top_bids(n);
    auto asks = b.top_asks(n);

    std::cout << "  top " << n << " bids:\n";
    for (auto& [px, sz] : bids) { std::cout << "    "; print_price_size(px, sz); std::cout << "\n"; }

    std::cout << "  top " << n << " asks:\n";
    for (auto& [px, sz] : asks) { std::cout << "    "; print_price_size(px, sz); std::cout << "\n"; }
}

int main() {
    const std::string canonical = "BTC-USD";
    const std::string cb_sym = SymbolCodec::to_venue("coinbase", canonical);

    // Build the per-venue pipeline (full-depth book)
    CbFeed feed{"coinbase", canonical, Backpressure::DropOldest};

    // Start WS + consumer
    feed.start_ws(cb_sym, /*port*/443);

    // Let it run and print a few snapshots
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        print_snapshot(feed.book(), 10);
    }

    feed.stop();
    return 0;
}

/*
Build:

cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
SIMDJSON_PREFIX=$(brew --prefix simdjson)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_coinbase.cpp \
  src/md/symbol_codec.cpp \
  test/test_pipeline_coinbase.cpp \
  -I src -I"$SIMDJSON_PREFIX/include" -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  -L"$SIMDJSON_PREFIX/lib" -lsimdjson \
  -L"$OPENSSL_PREFIX/lib" -lssl -lcrypto \
  -Wl,-rpath,"$SIMDJSON_PREFIX/lib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_pipeline_coinbase

./build/test_pipeline_coinbase
*/