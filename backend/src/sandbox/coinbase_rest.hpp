#pragma once
#include <string>
#include <functional>

// Snapshot of best bid/ask at time of fetch
struct BidAsk {
    double bid = 0.0;
    double ask = 0.0;
    std::string timestamp; // e.g. "2026-02-18 23:01:05.123 UTC"
};

// Full details of a placed order after fill
struct OrderDetails {
    std::string status;           // "open", "done", "settled", etc.
    double executed_value = 0.0; // total quote currency spent/received
    double filled_size = 0.0; // base currency amount filled
    double fill_fees = 0.0; // fees paid in quote currency
    double fill_price = 0.0; // executed_value / filled_size
};

// Simple REST client for placing Coinbase orders
class CoinbaseRest {
public:
    CoinbaseRest(std::string api_key, std::string api_secret,
        std::string passphrase, bool sandbox = true);

    // ©¤©¤ Generic market orders ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    // Buy base currency using quote_amount of the quote currency.
    // product_id examples: "BTC-USD", "ETH-USD", "ETH-BTC"
    // Returns order_id on success, empty string on failure.
    std::string buy_market(double quote_amount,
        const std::string& product_id = "BTC-USD");

    // Sell base currency worth quote_amount of the quote currency.
    // Returns order_id on success, empty string on failure.
    std::string sell_market(double quote_amount,
        const std::string& product_id = "BTC-USD");

    // Fetch best bid and ask from L1 order book for any product.
    BidAsk get_best_bid_ask(const std::string& product_id = "BTC-USD");

    // Fetch full order details (fill price, fees, status, etc.)
    OrderDetails get_order_details(const std::string& order_id);

    // Check if order is filled (status == "done" or "settled")
    bool is_order_filled(const std::string& order_id);

    std::string buy_btc_usd(double usd_amount) { return buy_market(usd_amount, "BTC-USD"); }
    std::string sell_btc_usd(double usd_amount) { return sell_market(usd_amount, "BTC-USD"); }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string passphrase_;
    std::string base_url_;

    std::string sign_request(const std::string& timestamp,
        const std::string& method,
        const std::string& path,
        const std::string& body);
    std::string base64_encode(const unsigned char* data, size_t len);
    std::string base64_decode(const std::string& str);

    std::string authenticated_get(const std::string& path);
};