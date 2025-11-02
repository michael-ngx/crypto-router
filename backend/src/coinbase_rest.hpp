#pragma once
#include <string>
#include <functional>

// Simple REST client for placing Coinbase orders
class CoinbaseRest {
public:
    CoinbaseRest(std::string api_key, std::string api_secret,
        std::string passphrase, bool sandbox = true);

    // Buy BTC with USD (market order)
    // Returns order_id on success, empty string on failure
    std::string buy_btc_usd(double usd_amount);

    // check if order filled
    bool is_order_filled(const std::string& order_id);

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
};