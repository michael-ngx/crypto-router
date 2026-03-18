#include "orderbook_analysis.hpp"
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

size_t curl_write_cb(void* ptr, size_t sz, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), sz * nmemb);
    return sz * nmemb;
}

std::string http_get(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("curl_easy_init failed");
    std::string body;
    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(c, CURLOPT_USERAGENT,      "CryptoRouter/1.0");
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(rc));
    return body;
}

// Handles both quoted ("96500.00","1.23") and bare (96500.0, 1.23) level arrays.
std::vector<std::pair<double,double>>
parse_levels(const std::string& json, const std::string& key) {
    std::vector<std::pair<double,double>> result;

    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return result;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return result;
    ++pos;

    while (pos < json.size()) {
        while (pos < json.size() && (json[pos]==' '||json[pos]=='\n'||
                                     json[pos]=='\r'||json[pos]==',')) ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '[') { ++pos; continue; }
        ++pos;

        double vals[2] = {0,0};
        int idx = 0;
        while (pos < json.size() && json[pos] != ']' && idx < 2) {
            while (pos < json.size() &&
                   (json[pos]==' '||json[pos]==','||json[pos]=='\n'||json[pos]=='\r')) ++pos;
            if (json[pos] == ']') break;
            bool q = (json[pos] == '"');
            if (q) ++pos;
            char* end = nullptr;
            double v = std::strtod(json.c_str() + pos, &end);
            if (end == json.c_str() + pos) { ++pos; continue; }
            pos = static_cast<size_t>(end - json.c_str());
            if (q && pos < json.size() && json[pos] == '"') ++pos;
            vals[idx++] = v;
        }
        if (idx >= 2 && vals[0] > 0.0 && vals[1] > 0.0)
            result.push_back({vals[0], vals[1]});
        pos = json.find(']', pos);
        if (pos != std::string::npos) ++pos;
    }
    return result;
}

std::string now_utc() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf) + "." + std::to_string(ms % 1000) + " UTC";
}

std::string fmt_usd(double v, int dp = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dp) << std::abs(v);
    std::string s = ss.str();
    size_t dot = s.find('.');
    size_t ie  = (dot == std::string::npos) ? s.size() : dot;
    for (int i = static_cast<int>(ie) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), ",");
    return (v < 0 ? "-$" : " $") + s;
}

std::string fmt_pct(double v) {
    std::ostringstream ss;
    ss << std::showpos << std::fixed << std::setprecision(4) << v << "%";
    return ss.str();
}

std::string fmt_btc(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8) << v;
    return ss.str();
}

std::string line(char c, int n) { return std::string(static_cast<size_t>(n), c); }

// Left-align / truncate to exact column width
std::string W(const std::string& s, int w) {
    int sw = static_cast<int>(s.size());
    if (sw >= w) return s.substr(0, static_cast<size_t>(w));
    return s + std::string(static_cast<size_t>(w - sw), ' ');
}

Orderbook build_book(std::vector<std::pair<double,double>> raw_bids,
                     std::vector<std::pair<double,double>> raw_asks,
                     int depth) {
    Orderbook ob;
    std::sort(raw_bids.begin(), raw_bids.end(),
              [](auto& a, auto& b){ return a.first > b.first; });
    std::sort(raw_asks.begin(), raw_asks.end(),
              [](auto& a, auto& b){ return a.first < b.first; });
    int bn = std::min(static_cast<int>(raw_bids.size()), depth);
    int an = std::min(static_cast<int>(raw_asks.size()), depth);
    ob.bids.reserve(bn);
    ob.asks.reserve(an);
    for (int i = 0; i < bn; ++i) ob.bids.push_back({raw_bids[i].first, raw_bids[i].second});
    for (int i = 0; i < an; ++i) ob.asks.push_back({raw_asks[i].first, raw_asks[i].second});
    ob.valid = !ob.bids.empty() && !ob.asks.empty();
    if (!ob.valid) ob.error = "Empty orderbook received";
    return ob;
}

} // anonymous namespace

Orderbook CoinbaseBookFetcher::fetch(const std::string& symbol, int depth) const {
    Orderbook ob;
    ob.exchange   = name();
    ob.symbol     = symbol;
    ob.native_sym = symbol;
    ob.timestamp  = now_utc();
    try {
        std::string body = http_get(
            "https://api.exchange.coinbase.com/products/" + symbol + "/book?level=2");
        auto bids = parse_levels(body, "bids");
        auto asks = parse_levels(body, "asks");
        ob = build_book(std::move(bids), std::move(asks), depth);
        ob.exchange   = name();
        ob.symbol     = symbol;
        ob.native_sym = symbol;
        ob.timestamp  = now_utc();
    } catch (const std::exception& e) { ob.error = e.what(); }
    return ob;
}

// ═════════════════════════════════════════════════════════════════════════════
// Kraken fetcher
// ═════════════════════════════════════════════════════════════════════════════

std::string KrakenBookFetcher::to_kraken_symbol(const std::string& sym) {
    if (sym.find('-') == std::string::npos) return sym;
    size_t d = sym.find('-');
    std::string base  = sym.substr(0, d);
    std::string quote = sym.substr(d + 1);
    if (base == "BTC") base = "XBT";  // Kraken uses XBT, not BTC
    for (char& c : base)  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : quote) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return base + quote;
}

Orderbook KrakenBookFetcher::fetch(const std::string& symbol, int depth) const {
    Orderbook ob;
    ob.exchange   = name();
    ob.symbol     = symbol;
    ob.native_sym = to_kraken_symbol(symbol);
    ob.timestamp  = now_utc();
    try {
        std::string body = http_get(
            "https://api.kraken.com/0/public/Depth?pair=" + ob.native_sym +
            "&count=" + std::to_string(depth));

        // Response wraps data under a variable pair key (e.g. "XXBTZUSD");
        // search from "result" to reliably find "bids"/"asks" regardless of key name.
        size_t result_pos = body.find("\"result\"");
        if (result_pos == std::string::npos) {
            ob.error = "\"result\" key not found; raw: " + body.substr(0, 200);
            return ob;
        }
        std::string section = body.substr(result_pos);
        auto bids = parse_levels(section, "bids");
        auto asks = parse_levels(section, "asks");
        ob = build_book(std::move(bids), std::move(asks), depth);
        ob.exchange   = name();
        ob.symbol     = symbol;
        ob.native_sym = to_kraken_symbol(symbol);
        ob.timestamp  = now_utc();
    } catch (const std::exception& e) { ob.error = e.what(); }
    return ob;
}

FillSim simulate_fill(const Orderbook& book, double quote_usd, bool buy_side) {
    FillSim s;
    s.quote_usd = quote_usd;
    const auto& levels = buy_side ? book.asks : book.bids;
    if (levels.empty()) return s;
    s.best_price = levels.front().price;

    double rem = quote_usd;
    for (const auto& lv : levels) {
        if (rem <= 0.0) break;
        double take_usd = std::min(rem, lv.price * lv.size);
        s.total_usd  += take_usd;
        s.total_base += take_usd / lv.price;
        rem          -= take_usd;
        ++s.levels_used;
    }
    s.fully_filled = (rem <= 1e-9);
    if (s.total_base > 1e-12)
        s.avg_fill_price = s.total_usd / s.total_base;
    if (s.best_price > 0.0 && s.avg_fill_price > 0.0)
        s.slippage_pct = buy_side
            ? (s.avg_fill_price - s.best_price) / s.best_price * 100.0
            : (s.best_price - s.avg_fill_price) / s.best_price * 100.0;
    return s;
}

namespace {

void write_snapshot(std::ofstream& f, const Orderbook& ob, int depth) {
    if (!ob.valid) { f << "  *** " << ob.error << " ***\n\n"; return; }

    double bid = ob.bids.front().price, ask = ob.asks.front().price;
    double spr = ask - bid;
    f << "  Fetched  : " << ob.timestamp << "\n"
      << "  Best Bid : " << fmt_usd(bid) << "\n"
      << "  Best Ask : " << fmt_usd(ask) << "\n"
      << "  Spread   : " << fmt_usd(spr)
      << "  (" << std::fixed << std::setprecision(5) << spr/bid*100 << "%)\n\n";

    // Side-by-side level table
    const int C = 43;
    std::string hdr = "  Price            Size(BTC)     USD Value";
    f << "  " << W("Bid Levels", C) << "  Ask Levels\n"
      << "  " << W(hdr, C) << "  " << hdr << "\n"
      << "  " << line('-', C) << "  " << line('-', C) << "\n";

    auto fmt_lv = [](const BookLevel& lv) {
        std::ostringstream ss;
        ss << "  " << std::fixed << std::setprecision(2) << std::setw(12) << lv.price
           << "  " << std::fixed << std::setprecision(8) << std::setw(13) << lv.size
           << "  " << std::setw(9) << static_cast<long long>(std::round(lv.price*lv.size));
        return ss.str();
    };

    int rows = std::min(std::max((int)ob.bids.size(),(int)ob.asks.size()), depth);
    for (int i = 0; i < rows; ++i) {
        std::string b = (i<(int)ob.bids.size()) ? fmt_lv(ob.bids[i]) : "";
        std::string a = (i<(int)ob.asks.size()) ? fmt_lv(ob.asks[i]) : "";
        f << "  " << W(b, C) << "  " << a << "\n";
    }
    f << "\n";
}

void write_fill_table(std::ofstream& f, const std::string& label,
                      const std::vector<FillSim>& sims) {
    const int A=16, B=18, C=20, D=14, E=9, G=10;
    f << "  Fill Simulation (" << label << ")\n"
      << "  " << W("USD Requested",A) << W("Avg Fill Price",B)
              << W("BTC Amount",C)    << W("Slippage",D)
              << W("Levels",E)        << W("Full Fill",G) << "\n"
      << "  " << line('-', A+B+C+D+E+G) << "\n";
    for (const auto& s : sims) {
        f << "  " << W(fmt_usd(s.quote_usd), A)
          << W(s.avg_fill_price > 0 ? fmt_usd(s.avg_fill_price) : "  N/A", B)
          << W(fmt_btc(s.total_base) + " BTC", C)
          << W(fmt_pct(s.slippage_pct), D)
          << W(std::to_string(s.levels_used), E)
          << W(s.fully_filled ? "Yes" : "PARTIAL", G) << "\n";
    }
    f << "\n";
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────

void run_analysis(const std::vector<IOrderbookFetcher*>& exchanges,
                  const std::string& symbol, int depth,
                  const std::vector<double>& amounts,
                  const std::string& output_file) {

    // Launch all fetches concurrently so orderbooks are captured as close to
    // the same instant as possible — important for meaningful arb comparison.
    std::cout << "Fetching orderbooks in parallel...\n";
    std::vector<std::future<Orderbook>> futures;
    futures.reserve(exchanges.size());
    for (auto* ex : exchanges)
        futures.push_back(std::async(std::launch::async,
                                     [ex, &symbol, depth]{ return ex->fetch(symbol, depth); }));

    std::vector<Orderbook> books;
    books.reserve(exchanges.size());
    for (size_t i = 0; i < futures.size(); ++i) {
        books.push_back(futures[i].get());
        const auto& ob = books.back();
        std::cout << "  " << ob.exchange << ": "
                  << (ob.valid ? "OK  @ " + ob.timestamp : "FAILED: " + ob.error) << "\n";
    }

    // Pre-compute all fill sims: [exchange][amount] = {buy, sell}
    using SP = std::pair<FillSim,FillSim>;
    std::vector<std::vector<SP>> sims(books.size(), std::vector<SP>(amounts.size()));
    for (size_t ei = 0; ei < books.size(); ++ei) {
        if (!books[ei].valid) continue;
        for (size_t ai = 0; ai < amounts.size(); ++ai) {
            sims[ei][ai] = { simulate_fill(books[ei], amounts[ai], true),
                             simulate_fill(books[ei], amounts[ai], false) };
        }
    }

    std::ofstream f(output_file);
    if (!f.is_open()) { std::cerr << "Cannot open: " << output_file << "\n"; return; }

    const int L = 80;
    f << line('=', L) << "\n"
      << "  ORDERBOOK FILL SIMULATION REPORT\n"
      << "  Symbol    : " << symbol << "\n"
      << "  Depth     : " << depth << " levels per side\n"
      << "  Amounts   :"; for (double a : amounts) f << "  " << fmt_usd(a); f << "\n"
      << "  Generated : " << now_utc() << "\n"
      << "  Exchanges :"; for (auto* ex : exchanges) f << "  " << ex->name(); f << "\n"
      << line('=', L) << "\n\n";

    for (size_t ei = 0; ei < books.size(); ++ei) {
        const auto& ob = books[ei];
        f << line('-', L) << "\n"
          << "  [" << ei+1 << "/" << books.size() << "]  "
          << ob.exchange << "  |  native symbol: " << ob.native_sym << "\n"
          << line('-', L) << "\n\n";
        write_snapshot(f, ob, depth);
        if (!ob.valid) continue;
        std::vector<FillSim> buy_v, sell_v;
        for (size_t ai = 0; ai < amounts.size(); ++ai) {
            buy_v.push_back(sims[ei][ai].first);
            sell_v.push_back(sims[ei][ai].second);
        }
        write_fill_table(f, "BUY  -- walking asks", buy_v);
        write_fill_table(f, "SELL -- walking bids", sell_v);
        f << "\n";
    }

    f << line('=', L) << "\n  CROSS-EXCHANGE SNAPSHOT SUMMARY\n" << line('=', L) << "\n\n"
      << "  " << W("Exchange",22) << W("Best Bid",18) << W("Best Ask",18)
              << W("Spread",14) << "Native Symbol\n"
      << "  " << line('-', L) << "\n";
    for (const auto& ob : books) {
        if (!ob.valid) { f << "  " << W(ob.exchange,22) << "(unavailable)\n"; continue; }
        double bid = ob.bids.front().price, ask = ob.asks.front().price;
        f << "  " << W(ob.exchange,22) << W(fmt_usd(bid),18)
          << W(fmt_usd(ask),18) << W(fmt_usd(ask-bid),14) << ob.native_sym << "\n";
    }
    f << "\n";

    auto slip_table = [&](const std::string& title, bool buy_side) {
        const int SW = 18;
        f << line('=', L) << "\n  " << title << "\n" << line('=', L) << "\n\n"
          << "  " << W("USD Amount", SW);
        for (const auto& ob : books) f << W(ob.exchange, SW);
        f << "\n  " << line('-', SW * (1 + (int)books.size())) << "\n";
        for (size_t ai = 0; ai < amounts.size(); ++ai) {
            f << "  " << W(fmt_usd(amounts[ai]), SW);
            for (size_t ei = 0; ei < books.size(); ++ei) {
                if (!books[ei].valid) { f << W("N/A", SW); continue; }
                const FillSim& s = buy_side ? sims[ei][ai].first : sims[ei][ai].second;
                std::string cell = fmt_pct(s.slippage_pct);
                if (!s.fully_filled) cell += "(partial)";
                f << W(cell, SW);
            }
            f << "\n";
        }
        f << "\n";
    };
    slip_table("SLIPPAGE COMPARISON -- BUY SIDE  (vs best ask)",  true);
    slip_table("SLIPPAGE COMPARISON -- SELL SIDE (vs best bid)", false);

    f << line('=', L) << "\n";
    f.close();
    std::cout << "\nReport written to: " << output_file << "\n";
}