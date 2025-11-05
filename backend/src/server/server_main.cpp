#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <thread>
#include <iostream>

#include "ws/ws.hpp"
#include "md/symbol_codec.hpp"
#include "md/book_parser_coinbase.hpp"
#include "md/book_parser_kraken.hpp"

#include "pipeline/venue_feed.hpp"
#include "pipeline/master_feed.hpp"
#include "server/http_server.hpp"
#include "server/http_routes.hpp"

using tcp = boost::asio::ip::tcp;

int main() {
    // Create VenueFeeds for coinbase and kraken BTC-USD
    using CbFeed = VenueFeed<CoinbaseWs, CoinbaseBookParser>;
    using KrFeed = VenueFeed<KrakenWs,  KrakenBookParser>;

    const std::string canonical = "BTC-USD";
    auto cb = std::make_shared<CbFeed>("coinbase", canonical, Backpressure::DropOldest, /*top_depth*/10);
    auto kr = std::make_shared<KrFeed>("kraken",  canonical, Backpressure::DropOldest, /*top_depth*/10);

    cb->start_ws(SymbolCodec::to_venue("coinbase", canonical), 443);
    kr->start_ws(SymbolCodec::to_venue("kraken",  canonical), 443);


    // Create UIMasterFeed and register venue feeds
    UIMasterFeed ui{canonical};
    ui.add_feed(cb);
    ui.add_feed(kr);


    // Start HTTP server
    boost::asio::io_context ioc{1};
    tcp::endpoint ep{boost::asio::ip::make_address("0.0.0.0"), 8080};
    HttpServer server{ioc, ep, [&](auto const& req, auto& res){
      handle_request(ui, req, res);
    }};
    server.run();

    std::cout << "HTTP listening on :8080\n";
    ioc.run();

    kr->stop();
    cb->stop();
    return 0;
}

/*
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
SIMDJSON_PREFIX=$(brew --prefix simdjson)

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_coinbase.cpp src/ws/ws_kraken.cpp \
  src/md/symbol_codec.cpp \
  src/pipeline/master_feed.cpp \
  src/server/server_main.cpp \
  -I src -I"$SIMDJSON_PREFIX/include" -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  -L"$SIMDJSON_PREFIX/lib" -lsimdjson \
  -L"$OPENSSL_PREFIX/lib" -lssl -lcrypto \
  -Wl,-rpath,"$SIMDJSON_PREFIX/lib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/md_server
*/