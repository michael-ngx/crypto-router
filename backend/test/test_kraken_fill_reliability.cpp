#include "kraken_rest.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <ctime>

// ── Config ─────────────────────────────────────────────────────
static constexpr int    DEFAULT_LOOPS        = 30;
static constexpr double DEFAULT_QUOTE_AMOUNT = 10.0;   // USD face value per order
static const std::string DEFAULT_SYMBOL      = "BTC-USD"; // converted to PI_XBTUSD
static constexpr int    POLL_INTERVAL_MS     = 500;
static constexpr int    FILL_TIMEOUT_S       = 30;
static constexpr int    INTER_ORDER_DELAY_S  = 1;

// ── Result record ──────────────────────────────────────────────
struct FillResult {
    int         loop_num;
    std::string side;            // "buy" or "sell"
    std::string symbol;          // Kraken symbol, e.g. "PI_XBTUSD"
    std::string order_timestamp;
    double      quote_amount;    // USD face value requested
    double      bid_at_order;
    double      ask_at_order;
    double      reference_price; // ask for buy, bid for sell
    std::string order_id;
    std::string fill_status;
    double      fill_price;
    double      executed_value;  // USD face value filled
    double      filled_size_base;// BTC equivalent
    double      fill_fees;       // USD
    double      slippage_pct;    // (fill - ref) / ref * 100
};

// ── Helpers ────────────────────────────────────────────────────
static std::string now_utc_string() {
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::time_t t = ms / 1000;
    std::tm* tm_info = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buf) + "." + std::to_string(ms % 1000) + " UTC";
}

static OrderDetails wait_for_fill(KrakenRest& client,
                                  const std::string& order_id,
                                  int timeout_s) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeout_s);
    OrderDetails d;
    while (std::chrono::steady_clock::now() < deadline) {
        d = client.get_order_details(order_id);
        // "filled" = fully done; "notFound" with a fill could happen too
        if (d.status == "filled") break;
        // If not in openOrders either → treat as filled (market order consumed)
        if (d.status == "notFound") {
            // Re-check fills one more time (race condition window)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            d = client.get_order_details(order_id);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
    return d;
}

// ── Report writer ──────────────────────────────────────────────
static void write_report(const std::vector<FillResult>& results,
                         const std::string& filename,
                         const std::string& display_symbol) {
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }

    // For PI_XBTUSD: base = XBT, quote = USD
    std::string base_cur  = "XBT";
    std::string quote_cur = "USD";
    size_t under = display_symbol.rfind('_');
    if (under != std::string::npos) {
        std::string body = display_symbol.substr(under + 1); // "XBTUSD"
        // Simple split: last 3 chars = quote, rest = base
        if (body.size() >= 6) {
            base_cur  = body.substr(0, body.size() - 3);
            quote_cur = body.substr(body.size() - 3);
        }
    }

    f << "=======================================================\n";
    f << "  Kraken Futures Market Order Fill Reliability Report\n";
    f << "  Symbol   : " << display_symbol
      << "  (1 contract = $1 USD face)\n";
    f << "  Generated: " << now_utc_string() << "\n";
    f << "=======================================================\n\n";

    f << std::left
      << std::setw(4)  << "#"
      << std::setw(5)  << "Side"
      << std::setw(28) << "Timestamp (UTC)"
      << std::setw(10) << "USD Amt"
      << std::setw(12) << "Contracts"
      << std::setw(14) << "Bid @ Order"
      << std::setw(14) << "Ask @ Order"
      << std::setw(14) << "Ref Price"
      << std::setw(14) << "Fill Price"
      << std::setw(12) << "Slippage%"
      << std::setw(12) << "Fees USD"
      << std::setw(16) << (base_cur + " Filled")
      << std::setw(10) << "Status"
      << "\n";
    f << std::string(175, '-') << "\n";

    double total_slippage = 0.0;
    int    filled_count   = 0;

    for (const auto& r : results) {
        long long contracts = static_cast<long long>(std::round(r.quote_amount));
        f << std::left
          << std::setw(4)  << r.loop_num
          << std::setw(5)  << r.side
          << std::setw(28) << r.order_timestamp
          << std::setw(10) << std::fixed << std::setprecision(2) << r.quote_amount
          << std::setw(12) << contracts
          << std::setw(14) << std::fixed << std::setprecision(2) << r.bid_at_order
          << std::setw(14) << std::fixed << std::setprecision(2) << r.ask_at_order
          << std::setw(14) << std::fixed << std::setprecision(2) << r.reference_price
          << std::setw(14) << std::fixed << std::setprecision(2) << r.fill_price
          << std::setw(12) << std::fixed << std::setprecision(4) << r.slippage_pct
          << std::setw(12) << std::fixed << std::setprecision(4) << r.fill_fees
          << std::setw(16) << std::fixed << std::setprecision(8) << r.filled_size_base
          << std::setw(10) << r.fill_status
          << "\n";

        if (r.fill_status == "filled") {
            total_slippage += r.slippage_pct;
            ++filled_count;
        }
    }
    f << std::string(175, '-') << "\n\n";

    // Per-order detail
    f << "=======================================================\n";
    f << "  Per-Order Detail\n";
    f << "=======================================================\n\n";
    for (const auto& r : results) {
        long long contracts = static_cast<long long>(std::round(r.quote_amount));
        f << "Order #" << r.loop_num << " (" << r.side << " " << display_symbol << ")\n";
        f << "  Order ID        : " << r.order_id << "\n";
        f << "  Timestamp       : " << r.order_timestamp << "\n";
        f << "  USD Face Amount : " << std::fixed << std::setprecision(2)
          << r.quote_amount << " USD  (" << contracts << " contracts)\n";
        f << "  Bid at Order    : " << std::fixed << std::setprecision(2)
          << r.bid_at_order  << " " << quote_cur << "\n";
        f << "  Ask at Order    : " << std::fixed << std::setprecision(2)
          << r.ask_at_order  << " " << quote_cur << "\n";
        f << "  Spread          : " << std::fixed << std::setprecision(2)
          << (r.ask_at_order - r.bid_at_order) << " " << quote_cur << "\n";
        f << "  Reference Price : " << std::fixed << std::setprecision(2)
          << r.reference_price << " " << quote_cur
          << (r.side == "buy" ? " (ask)" : " (bid)") << "\n";
        f << "  Fill Price      : " << std::fixed << std::setprecision(2)
          << r.fill_price    << " " << quote_cur << "\n";
        f << "  Slippage        : " << std::fixed << std::setprecision(4)
          << r.slippage_pct  << "%\n";
        f << "  Executed Value  : " << std::fixed << std::setprecision(2)
          << r.executed_value << " USD (face)\n";
        f << "  " << base_cur << " Equivalent : "
          << std::fixed << std::setprecision(8)
          << r.filled_size_base << " " << base_cur << "\n";
        f << "  Fees Paid       : " << std::fixed << std::setprecision(6)
          << r.fill_fees     << " USD\n";
        f << "  Status          : " << r.fill_status << "\n\n";
    }

    // Summary
    f << "=======================================================\n";
    f << "  Summary Statistics\n";
    f << "=======================================================\n";
    f << "  Symbol              : " << display_symbol << "\n";
    f << "  Total orders placed : " << results.size()  << "\n";
    f << "  Orders filled       : " << filled_count    << "\n";
    if (filled_count > 0) {
        f << "  Avg slippage (%%)    : "
          << std::fixed << std::setprecision(4)
          << (total_slippage / filled_count) << "%\n";
    }
    f << "\n";
    f.close();
    std::cout << "\nReport written to: " << filename << "\n";
}

// ── Main ───────────────────────────────────────────────────────
// Usage: test_kraken_fill_reliability [loops] [quote_amount] [symbol]
// Example: test_kraken_fill_reliability 10 10.0 BTC-USD
int main(int argc, char* argv[]) {
    int         loops        = DEFAULT_LOOPS;
    double      quote_amount = DEFAULT_QUOTE_AMOUNT;
    std::string symbol       = DEFAULT_SYMBOL;

    if (argc >= 2) loops        = std::stoi(argv[1]);
    if (argc >= 3) quote_amount = std::stod(argv[2]);
    if (argc >= 4) symbol       = argv[3];

    // Demo (sandbox) credentials
    std::string api_key    = "";
    std::string api_secret = "";
    bool sandbox = true;

    KrakenRest client(api_key, api_secret, sandbox);

    // Convert to Kraken symbol for display
    std::string ks = symbol;
    if (ks.find('_') == std::string::npos) {
        auto dash   = ks.find('-');
        std::string base  = (dash != std::string::npos) ? ks.substr(0, dash) : ks;
        std::string quote = (dash != std::string::npos) ? ks.substr(dash + 1) : "USD";
        if (base == "BTC") base = "XBT";
        for (char& c : base)  c = static_cast<char>(toupper(c));
        for (char& c : quote) c = static_cast<char>(toupper(c));
        ks = "PI_" + base + quote;
    }

    long long contracts_per_order = static_cast<long long>(std::round(quote_amount));

    std::cout << "Kraken Futures Fill Reliability Test\n";
    std::cout << "  Symbol     : " << ks << "\n";
    std::cout << "  Loops      : " << loops << "\n";
    std::cout << "  Amount     : " << std::fixed << std::setprecision(2)
              << quote_amount << " USD face (" << contracts_per_order
              << " contracts) per order\n";
    std::cout << "  Buy orders : " << (loops / 2 + loops % 2) << "\n";
    std::cout << "  Sell orders: " << (loops / 2) << "\n";
    std::cout << "  Sandbox    : " << (sandbox ? "YES (demo-futures.kraken.com)" : "NO — LIVE!")
              << "\n\n";

    std::vector<FillResult> results;
    results.reserve(loops);

    for (int i = 1; i <= loops; i++) {
        bool        is_buy = (i % 2 != 0);
        std::string side   = is_buy ? "buy" : "sell";

        std::cout << "── Order " << i << "/" << loops
                  << " (" << side << " " << ks << ") ──\n";

        // 1. Snapshot bid/ask
        std::cout << "  Fetching bid/ask...\n";
        BidAsk ba = client.get_best_bid_ask(symbol);
        std::cout << "  Bid: " << std::fixed << std::setprecision(2) << ba.bid
                  << "  Ask: " << ba.ask << " USD\n";

        // 2. Record timestamp and place order
        std::string order_ts = now_utc_string();
        std::string order_id = is_buy
            ? client.buy_market(quote_amount, symbol)
            : client.sell_market(quote_amount, symbol);

        if (order_id.empty()) {
            std::cerr << "  Failed to place order, skipping.\n";
            FillResult r{};
            r.loop_num       = i;
            r.side           = side;
            r.symbol         = ks;
            r.order_timestamp = order_ts;
            r.quote_amount   = quote_amount;
            r.bid_at_order   = ba.bid;
            r.ask_at_order   = ba.ask;
            r.fill_status    = "FAILED";
            results.push_back(r);
            continue;
        }
        std::cout << "  Order ID: " << order_id << "\n";

        // 3. Wait for fill
        std::cout << "  Waiting for fill...\n";
        OrderDetails d = wait_for_fill(client, order_id, FILL_TIMEOUT_S);
        std::cout << "  Status    : " << d.status     << "\n";
        std::cout << "  Fill price: " << std::fixed << std::setprecision(2)
                  << d.fill_price    << " USD\n";

        // 4. Slippage: buy vs ask, sell vs bid
        double ref_price    = is_buy ? ba.ask : ba.bid;
        double slippage_pct = 0.0;
        if (ref_price > 0.0 && d.fill_price > 0.0)
            slippage_pct = (d.fill_price - ref_price) / ref_price * 100.0;

        std::cout << "  Slippage  : " << std::fixed << std::setprecision(4)
                  << slippage_pct << "%\n";

        FillResult r;
        r.loop_num        = i;
        r.side            = side;
        r.symbol          = ks;
        r.order_timestamp = order_ts;
        r.quote_amount    = quote_amount;
        r.bid_at_order    = ba.bid;
        r.ask_at_order    = ba.ask;
        r.reference_price = ref_price;
        r.order_id        = order_id;
        r.fill_status     = d.status;
        r.fill_price      = d.fill_price;
        r.executed_value  = d.executed_value;
        r.filled_size_base = d.filled_size;
        r.fill_fees       = d.fill_fees;
        r.slippage_pct    = slippage_pct;
        results.push_back(r);

        if (i < loops) {
            std::cout << "  Waiting " << INTER_ORDER_DELAY_S << "s before next order...\n\n";
            std::this_thread::sleep_for(std::chrono::seconds(INTER_ORDER_DELAY_S));
        }
    }

    // 5. Write report — safe filename from symbol
    std::string safe_sym = ks;
    for (char& c : safe_sym) if (c == '_') c = '-';
    write_report(results, "kraken_fill_reliability_" + safe_sym + ".txt", ks);
    return 0;
}