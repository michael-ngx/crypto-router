#include "web_socket.hpp"
#include <iostream>
#include <atomic>
#include <thread>

int main(){
    auto start = std::chrono::steady_clock::now();
    CoinbaseWs ws{"BTC-USD", [&](const std::string& msg){
        std::cout << msg << "\n";
        // Optional: basic filter
        // if (msg.find("\"channel\":\"ticker\"") != std::string::npos) { ... }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(20)) {
            std::cout << "Stopping after 20s...\n";
            ws.stop();
        }
    }};
    ws.start(443);
    return 0;
}
