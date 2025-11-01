#include "../src/ws/ws.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

int main(){
    auto start = std::chrono::steady_clock::now();

    KrakenWs ws{"BTC/USD", [&](const std::string& msg){
        std::cout << msg << "\n";
        // For testing purposes, stop after 5s
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            std::cout << "Stopping after 5 seconds...\n";
            ws.stop();
        }
    }, "bbo"};

    ws.start(443);
    return 0;
}

/*
clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_kraken.cpp test/test_kraken.cpp \
  -I src -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  "$OPENSSL_PREFIX/lib/libssl.dylib" \
  "$OPENSSL_PREFIX/lib/libcrypto.dylib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_kraken

./build/test_kraken
*/