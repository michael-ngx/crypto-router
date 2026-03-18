#include "kraken_rest.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // Demo (sandbox) credentials — demo-futures.kraken.com
    std::string api_key    = "";
    std::string api_secret = "";

    // sandbox = true  → demo-futures.kraken.com
    KrakenRest client(api_key, api_secret, /*sandbox=*/true);

    // ── Quick bid/ask check (public endpoint) ──────────────────
    std::cout << "Fetching PI_XBTUSD bid/ask...\n";
    BidAsk ba = client.get_best_bid_ask("BTC-USD");
    std::cout << "  Bid: " << ba.bid << "  Ask: " << ba.ask
              << "  @ " << ba.timestamp << "\n\n";

    // ── Place a $10 long (buy) ─────────────────────────────────
    // On PI_XBTUSD, 1 contract = $1 face, so this sends size=10
    std::cout << "Placing $10 buy order on PI_XBTUSD...\n";
    std::string order_id = client.buy_btc_usd(10.0);

    if (!order_id.empty()) {
        std::cout << "Order placed! ID: " << order_id << "\n";

        // Wait briefly then check fill status
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (client.is_order_filled(order_id)) {
            std::cout << "Order filled!\n";

            OrderDetails d = client.get_order_details(order_id);
            std::cout << "  Status         : " << d.status        << "\n";
            std::cout << "  Fill price     : " << d.fill_price    << " USD/BTC\n";
            std::cout << "  Executed value : " << d.executed_value << " USD\n";
            std::cout << "  BTC received   : " << d.filled_size   << " BTC\n";
            std::cout << "  Fees paid      : " << d.fill_fees     << " USD\n";
        } else {
            std::cout << "Order still pending (market orders should fill instantly).\n";
            OrderDetails d = client.get_order_details(order_id);
            std::cout << "  Status: " << d.status << "\n";
        }
    } else {
        std::cout << "Failed to place order. Check credentials and account balance.\n";
    }

    return 0;
}