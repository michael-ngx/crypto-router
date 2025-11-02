#include "coinbase_rest.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // Sandbox credentials
    std::string api_key = "";
    std::string api_secret = "";  // base64 encoded
    std::string passphrase = "";

    CoinbaseRest client(api_key, api_secret, passphrase, true);

    std::cout << "Buying $10 worth of BTC...\n";
    std::string order_id = client.buy_btc_usd(10.0);

    if (!order_id.empty()) {
        std::cout << "Order placed! ID: " << order_id << "\n";

        // Wait a bit and check if filled
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (client.is_order_filled(order_id)) {
            std::cout << "Order filled successfully!\n";
        }
        else {
            std::cout << "Order still pending...\n";
        }
    }
    else {
        std::cout << "Failed to place order\n";
    }

    return 0;
}