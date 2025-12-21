#include "../src/venues/coinbase/ws.hpp"
#include <iostream>
#include <atomic>
#include <thread>

int main(){
    auto start = std::chrono::steady_clock::now();
    CoinbaseWs ws{"BTC-USD", [&](const std::string& msg){
        std::cout << msg << "\n";
        // For testing purposes, stop after 1s
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) {
            std::cout << "Stopping after 1 seconds...\n";
            ws.stop();
        }
    }};
    ws.start(443);
    return 0;
}

/*
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/venues/coinbase/ws.cpp test/test_ws_coinbase.cpp \
  -I src -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  "$OPENSSL_PREFIX/lib/libssl.dylib" \
  "$OPENSSL_PREFIX/lib/libcrypto.dylib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_ws_coinbase

./build/test_ws_coinbase
*/
