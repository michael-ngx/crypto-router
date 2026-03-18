#pragma once
#include <string>
#include <vector>

// Snapshot of best bid/ask at time of fetch
struct BidAsk {
    double bid = 0.0;
    double ask = 0.0;
    std::string timestamp; // e.g. "2026-02-18 23:01:05.123 UTC"
};

// Full details of a placed order after fill
struct OrderDetails {
    std::string status;           // "open", "filled", "cancelled", "notFound"
    double executed_value = 0.0; // total USD face value traded (= size contracts × $1 for PI_*)
    double filled_size    = 0.0; // base currency (BTC) amount = executed_value / fill_price
    double fill_fees      = 0.0; // fees paid, converted to USD
    double fill_price     = 0.0; // average fill price in USD
};

// ============================================================
// KrakenRest — Kraken Futures REST client
//
// Pair naming: accepts Coinbase-style ("BTC-USD", "ETH-USD") or
// native Kraken Futures style ("PI_XBTUSD", "PI_ETHUSD").
// Coinbase-style pairs are automatically converted.
//
// Contract sizing (PI_* inverse perpetuals):
//   1 contract = $1 USD face value
//   so size = round(quote_amount)
//
// sandbox = true  → https://demo-futures.kraken.com
// sandbox = false → https://futures.kraken.com
// ============================================================
class KrakenRest {
public:
    KrakenRest(std::string api_key, std::string api_secret,
               bool sandbox = true);

    // Buy: opens/increases a long position worth ~quote_amount USD.
    // symbol examples: "BTC-USD", "ETH-USD", "PI_XBTUSD"
    // Returns order_id on success, empty string on failure.
    std::string buy_market(double quote_amount,
                           const std::string& symbol = "BTC-USD");

    // Sell: opens/increases a short position worth ~quote_amount USD.
    // Returns order_id on success, empty string on failure.
    std::string sell_market(double quote_amount,
                            const std::string& symbol = "BTC-USD");

    // Fetch best bid and ask from the public L1 order book.
    BidAsk get_best_bid_ask(const std::string& symbol = "BTC-USD");

    // Fetch full order details.
    // Checks /openorders first (pending), then /fills (completed).
    OrderDetails get_order_details(const std::string& order_id);

    // Returns true if the order has a fill record in /fills.
    bool is_order_filled(const std::string& order_id);

    // Convenience aliases (Coinbase naming compatibility)
    std::string buy_btc_usd(double usd_amount)  { return buy_market(usd_amount,  "BTC-USD"); }
    std::string sell_btc_usd(double usd_amount) { return sell_market(usd_amount, "BTC-USD"); }

private:
    std::string api_key_;
    std::string api_secret_;   // base64-encoded secret from Kraken
    std::string base_url_;

    // Convert "BTC-USD" → "PI_XBTUSD";  "PI_XBTUSD" passes through unchanged
    std::string to_kraken_symbol(const std::string& pair) const;

    // Authent = base64( HMAC-SHA512( base64_decode(secret),
    //                                SHA256(postData + nonce + endpointPath) ) )
    std::string compute_authent(const std::string& post_data,
                                const std::string& nonce,
                                const std::string& endpoint_path) const;

    std::string                base64_encode_str(const unsigned char* data, size_t len) const;
    std::vector<unsigned char> base64_decode_bytes(const std::string& str) const;

    // HTTP helpers
    std::string public_get(const std::string& path) const;
    std::string authenticated_get(const std::string& path) const;
    std::string authenticated_post(const std::string& path,
                                   const std::string& body) const;

    // Lightweight JSON field extractors (no external deps, mirrors coinbase style)
    static std::string parse_str(const std::string& json, const std::string& key);
    static double      parse_dbl(const std::string& json, const std::string& key);
};