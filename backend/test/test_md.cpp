#include "ws/ws.hpp" // IMarketWs
#include "md/md_types.hpp"
#include "md/symbol_codec.hpp"
#include "md/md_normalizer.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

// forward decls for the two normalizer factories (defined in the .cpp files above)
IMarketNormalizer *make_coinbase_normalizer();
IMarketNormalizer *make_kraken_normalizer();

static void print_tick(const NormalizedTick &t)
{
    static std::mutex io_mtx;
    std::lock_guard<std::mutex> lk(io_mtx);
    std::cout << t.venue << " " << t.symbol
              << " bid=" << t.bid
              << " ask=" << t.ask
              << " last=" << t.last
              << " ts_ns=" << t.ts_ns << "\n";
}

int main()
{
    // Build normalizers
    std::unique_ptr<IMarketNormalizer> cb_norm(make_coinbase_normalizer());
    std::unique_ptr<IMarketNormalizer> kr_norm(make_kraken_normalizer());

    // Coinbase wants "BTC-USD"
    const std::string cb_sym = SymbolCodec::to_venue("coinbase", "BTC-USD");
    // Kraken wants "BTC/USD"
    const std::string kr_sym = SymbolCodec::to_venue("kraken", "BTC-USD");

    // Build WS connectors with raw callbacks that normalize then print
    CoinbaseWs cb_ws{cb_sym, [&](const std::string &raw)
                    {
                        NormalizedTick t{};
                        if (cb_norm->parse_ticker(raw, t))
                            print_tick(t);
                    }};
    KrakenWs kr_ws{kr_sym, [&](const std::string &raw)
                    {
                       NormalizedTick t{};
                       if (kr_norm->parse_ticker(raw, t))
                           print_tick(t);
                    },
                    "bbo"};

    // Run both connectors concurrently (each blocks in start())
    std::thread t1([&]{ cb_ws.start(443); });
    std::thread t2([&]{ kr_ws.start(443); });

    // Let it run for ~25 seconds then stop
    std::this_thread::sleep_for(std::chrono::seconds(25));
    cb_ws.stop();
    kr_ws.stop();

    t1.join();
    t2.join();
    
    return 0;
}


/*
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_coinbase.cpp src/ws/ws_kraken.cpp \
  src/md/md_normalizer_coinbase.cpp src/md/md_normalizer_kraken.cpp \
  src/md/symbol_codec.cpp \
  test/test_md.cpp \
  -I src -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  "$OPENSSL_PREFIX/lib/libssl.dylib" \
  "$OPENSSL_PREFIX/lib/libcrypto.dylib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_feeds

./build/test_feeds
*/