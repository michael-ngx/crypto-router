#include "coinbase_rest.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <ctime>

// ©¤©¤ Config ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static constexpr int    DEFAULT_LOOPS = 10;
static constexpr double DEFAULT_QUOTE_AMOUNT = 10.0;
static const std::string DEFAULT_PRODUCT_ID = "BTC-USD";
static constexpr int    POLL_INTERVAL_MS = 500;  // re-check fill status interval
static constexpr int    FILL_TIMEOUT_S = 30;   // give up waiting after this long
static constexpr int    INTER_ORDER_DELAY_S = 1;    // pause between orders

// ©¤©¤ Result record ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
struct FillResult {
    int         loop_num;
    std::string side;             // "buy" or "sell"
    std::string product_id;
    std::string order_timestamp;
    double      quote_amount;
    double      bid_at_order;
    double      ask_at_order;
    double      reference_price;  // ask for buy, bid for sell
    std::string order_id;
    std::string fill_status;
    double      fill_price;
    double      executed_value;
    double      filled_size_base; // base currency (e.g. BTC)
    double      fill_fees;
    double      slippage_pct;     // (fill - ref) / ref * 100, +ve = paid more than expected
};

// ©¤©¤ Helpers ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static std::string now_utc_string() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::time_t t = ms / 1000;
    std::tm* tm_info = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buf) + "." + std::to_string(ms % 1000) + " UTC";
}

static OrderDetails wait_for_fill(CoinbaseRest& client,
    const std::string& order_id,
    int timeout_s) {
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(timeout_s);
    OrderDetails d;
    while (std::chrono::steady_clock::now() < deadline) {
        d = client.get_order_details(order_id);
        if (d.status == "done" || d.status == "settled") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
    return d;
}

// ©¤©¤ Report writer ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static void write_report(const std::vector<FillResult>& results,
    const std::string& filename,
    const std::string& product_id) {
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }

    // Split product_id into base/quote for display (e.g. "BTC" / "USD")
    std::string base_cur = product_id;
    std::string quote_cur = product_id;
    size_t dash = product_id.find('-');
    if (dash != std::string::npos) {
        base_cur = product_id.substr(0, dash);
        quote_cur = product_id.substr(dash + 1);
    }

    f << "=======================================================\n";
    f << "  Coinbase Market Order Fill Reliability Report\n";
    f << "  Product  : " << product_id << "\n";
    f << "  Generated: " << now_utc_string() << "\n";
    f << "=======================================================\n\n";

    // Summary table
    f << std::left
        << std::setw(4) << "#"
        << std::setw(5) << "Side"
        << std::setw(28) << "Timestamp (UTC)"
        << std::setw(12) << (quote_cur + " Amt")
        << std::setw(14) << "Bid @ Order"
        << std::setw(14) << "Ask @ Order"
        << std::setw(14) << "Ref Price"
        << std::setw(14) << "Fill Price"
        << std::setw(12) << "Slippage%"
        << std::setw(12) << ("Fees " + quote_cur)
        << std::setw(16) << (base_cur + " Filled")
        << std::setw(10) << "Status"
        << "\n";
    f << std::string(165, '-') << "\n";

    double total_slippage = 0.0;
    int    filled_count = 0;

    for (const auto& r : results) {
        f << std::left
            << std::setw(4) << r.loop_num
            << std::setw(5) << r.side
            << std::setw(28) << r.order_timestamp
            << std::setw(12) << std::fixed << std::setprecision(2) << r.quote_amount
            << std::setw(14) << std::fixed << std::setprecision(2) << r.bid_at_order
            << std::setw(14) << std::fixed << std::setprecision(2) << r.ask_at_order
            << std::setw(14) << std::fixed << std::setprecision(2) << r.reference_price
            << std::setw(14) << std::fixed << std::setprecision(2) << r.fill_price
            << std::setw(12) << std::fixed << std::setprecision(4) << r.slippage_pct
            << std::setw(12) << std::fixed << std::setprecision(4) << r.fill_fees
            << std::setw(16) << std::fixed << std::setprecision(8) << r.filled_size_base
            << std::setw(10) << r.fill_status
            << "\n";

        if (r.fill_status == "done" || r.fill_status == "settled") {
            total_slippage += r.slippage_pct;
            ++filled_count;
        }
    }

    f << std::string(165, '-') << "\n\n";

    // Per-order detail block
    f << "=======================================================\n";
    f << "  Per-Order Detail\n";
    f << "=======================================================\n\n";
    for (const auto& r : results) {
        f << "Order #" << r.loop_num << " (" << r.side << " " << product_id << ")\n";
        f << "  Order ID        : " << r.order_id << "\n";
        f << "  Timestamp       : " << r.order_timestamp << "\n";
        f << "  Amount          : " << std::fixed << std::setprecision(2) << r.quote_amount << " " << quote_cur << "\n";
        f << "  Bid at Order    : " << std::fixed << std::setprecision(2) << r.bid_at_order << " " << quote_cur << "\n";
        f << "  Ask at Order    : " << std::fixed << std::setprecision(2) << r.ask_at_order << " " << quote_cur << "\n";
        f << "  Spread          : " << std::fixed << std::setprecision(2) << (r.ask_at_order - r.bid_at_order) << " " << quote_cur << "\n";
        f << "  Reference Price : " << std::fixed << std::setprecision(2) << r.reference_price << " " << quote_cur
            << (r.side == "buy" ? " (ask)" : " (bid)") << "\n";
        f << "  Fill Price      : " << std::fixed << std::setprecision(2) << r.fill_price << " " << quote_cur << "\n";
        f << "  Slippage        : " << std::fixed << std::setprecision(4) << r.slippage_pct << "%\n";
        f << "  Executed Value  : " << std::fixed << std::setprecision(6) << r.executed_value << " " << quote_cur << "\n";
        f << "  Base Filled     : " << std::fixed << std::setprecision(8) << r.filled_size_base << " " << base_cur << "\n";
        f << "  Fees Paid       : " << std::fixed << std::setprecision(6) << r.fill_fees << " " << quote_cur << "\n";
        f << "  Status          : " << r.fill_status << "\n\n";
    }

    // Summary stats
    f << "=======================================================\n";
    f << "  Summary Statistics\n";
    f << "=======================================================\n";
    f << "  Product             : " << product_id << "\n";
    f << "  Total orders placed : " << results.size() << "\n";
    f << "  Orders filled       : " << filled_count << "\n";
    if (filled_count > 0) {
        f << "  Avg slippage (%%)    : "
            << std::fixed << std::setprecision(4)
            << (total_slippage / filled_count) << "%\n";
    }
    f << "\n";
    f.close();
    std::cout << "\nReport written to: " << filename << "\n";
}

// ©¤©¤ Main ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
// Usage: test_fill_reliability [loops] [quote_amount] [product_id]
// Example: test_fill_reliability 10 10.0 ETH-USD
int main(int argc, char* argv[]) {
    int         loops = DEFAULT_LOOPS;
    double      quote_amount = DEFAULT_QUOTE_AMOUNT;
    std::string product_id = DEFAULT_PRODUCT_ID;

    if (argc >= 2) loops = std::stoi(argv[1]);
    if (argc >= 3) quote_amount = std::stod(argv[2]);
    if (argc >= 4) product_id = argv[3];

    // Split for display
    std::string base_cur = product_id;
    std::string quote_cur = product_id;
    size_t dash = product_id.find('-');
    if (dash != std::string::npos) {
        base_cur = product_id.substr(0, dash);
        quote_cur = product_id.substr(dash + 1);
    }

    std::cout << "Fill Reliability Test\n";
    std::cout << "  Product    : " << product_id << "\n";
    std::cout << "  Loops      : " << loops << "\n";
    std::cout << "  Amount     : " << std::fixed << std::setprecision(2)
        << quote_amount << " " << quote_cur << " per order\n";
    std::cout << "  Buy orders : " << (loops / 2 + loops % 2) << "\n";
    std::cout << "  Sell orders: " << (loops / 2) << "\n\n";

    // Credentials
    std::string api_key = "1ab9d3c73fd8c106bb6d22360997cf47";
    std::string api_secret = "pTyE9q8QHh0GuW02V+Na6+8mEIAgx5dpOQnfn9pIOutNZgc3280Oqb/UEXOyMJ2wPkFLIc18thrWyY3FcGw3LQ==";
    std::string passphrase = "eb8i00hy4ise";
    bool sandbox = true;

    CoinbaseRest client(api_key, api_secret, passphrase, sandbox);

    std::vector<FillResult> results;
    results.reserve(loops);

    for (int i = 1; i <= loops; i++) {
        bool is_buy = (i % 2 != 0); // odd = buy, even = sell
        std::string side = is_buy ? "buy" : "sell";

        std::cout << "©¤©¤ Order " << i << "/" << loops
            << " (" << side << " " << product_id << ") ©¤©¤\n";

        // 1. Snapshot bid/ask
        std::cout << "  Fetching bid/ask...\n";
        BidAsk ba = client.get_best_bid_ask(product_id);
        std::cout << "  Bid: " << std::fixed << std::setprecision(2) << ba.bid
            << "  Ask: " << ba.ask << " " << quote_cur << "\n";

        // 2. Timestamp and place order
        std::string order_ts = now_utc_string();
        std::string order_id = is_buy
            ? client.buy_market(quote_amount, product_id)
            : client.sell_market(quote_amount, product_id);

        if (order_id.empty()) {
            std::cerr << "  Failed to place order, skipping.\n";
            FillResult r{};
            r.loop_num = i;
            r.side = side;
            r.product_id = product_id;
            r.order_timestamp = order_ts;
            r.quote_amount = quote_amount;
            r.bid_at_order = ba.bid;
            r.ask_at_order = ba.ask;
            r.fill_status = "FAILED";
            results.push_back(r);
            continue;
        }
        std::cout << "  Order ID: " << order_id << "\n";

        // 3. Wait for fill
        std::cout << "  Waiting for fill...\n";
        OrderDetails d = wait_for_fill(client, order_id, FILL_TIMEOUT_S);
        std::cout << "  Status    : " << d.status << "\n";
        std::cout << "  Fill price: " << std::fixed << std::setprecision(2)
            << d.fill_price << " " << quote_cur << "\n";

        // 4. Slippage: buy vs ask, sell vs bid
        double ref_price = is_buy ? ba.ask : ba.bid;
        double slippage_pct = 0.0;
        if (ref_price > 0.0 && d.fill_price > 0.0)
            slippage_pct = (d.fill_price - ref_price) / ref_price * 100.0;

        std::cout << "  Slippage  : " << std::fixed << std::setprecision(4)
            << slippage_pct << "%\n";

        FillResult r;
        r.loop_num = i;
        r.side = side;
        r.product_id = product_id;
        r.order_timestamp = order_ts;
        r.quote_amount = quote_amount;
        r.bid_at_order = ba.bid;
        r.ask_at_order = ba.ask;
        r.reference_price = ref_price;
        r.order_id = order_id;
        r.fill_status = d.status;
        r.fill_price = d.fill_price;
        r.executed_value = d.executed_value;
        r.filled_size_base = d.filled_size;
        r.fill_fees = d.fill_fees;
        r.slippage_pct = slippage_pct;
        results.push_back(r);

        if (i < loops) {
            std::cout << "  Waiting " << INTER_ORDER_DELAY_S << "s before next order...\n\n";
            std::this_thread::sleep_for(std::chrono::seconds(INTER_ORDER_DELAY_S));
        }
    }

    // 5. Write report ¡ª filename includes product so multiple runs don't overwrite
    std::string safe_product = product_id;
    for (char& c : safe_product) if (c == '-') c = '_'; // BTC-USD ¡ú BTC_USD
    write_report(results, "fill_reliability_" + safe_product + ".txt", product_id);
    return 0;
}