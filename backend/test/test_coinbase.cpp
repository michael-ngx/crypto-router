#include "../src/ws/ws.hpp"
#include <iostream>
#include <atomic>
#include <thread>

int main(){
    auto start = std::chrono::steady_clock::now();
    CoinbaseWs ws{"BTC-USD", [&](const std::string& msg){
        std::cout << msg << "\n";
        // For testing purposes, stop after 5s
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            std::cout << "Stopping after 5 seconds...\n";
            ws.stop();
        }
    }};
    ws.start(443);
    return 0;
}

/*
/*
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_coinbase.cpp test/test_coinbase.cpp \
  -I src -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  "$OPENSSL_PREFIX/lib/libssl.dylib" \
  "$OPENSSL_PREFIX/lib/libcrypto.dylib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_coinbase

./build/test_coinbase
*/