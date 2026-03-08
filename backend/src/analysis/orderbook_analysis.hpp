#pragma once
#include <string>
#include <vector>

struct BookLevel {
    double price = 0.0;
    double size  = 0.0;   // base currency (BTC for *-USD pairs)
};

struct Orderbook {
    std::string            exchange;
    std::string            symbol;      // canonical, e.g. "BTC-USD"
    std::string            native_sym;  // exchange-native, e.g. "XBTUSD"
    std::string            timestamp;
    std::vector<BookLevel> bids;        // sorted desc
    std::vector<BookLevel> asks;        // sorted asc
    bool                   valid = false;
    std::string            error;
};

struct FillSim {
    double quote_usd      = 0.0;
    double avg_fill_price = 0.0;
    double total_base     = 0.0;
    double total_usd      = 0.0;
    double best_price     = 0.0;
    double slippage_pct   = 0.0;  // +ve = taker paid more / received less than best
    int    levels_used    = 0;
    bool   fully_filled   = false;
};


class IOrderbookFetcher {
public:
    virtual ~IOrderbookFetcher() = default;
    virtual std::string name() const = 0;
    virtual Orderbook fetch(const std::string& symbol, int depth) const = 0;
};

// Coinbase Exchange — GET api.exchange.coinbase.com/products/{pair}/book?level=2
class CoinbaseBookFetcher : public IOrderbookFetcher {
public:
    std::string name() const override { return "Coinbase"; }
    Orderbook   fetch(const std::string& symbol, int depth) const override;
};

// Kraken — GET api.kraken.com/0/public/Depth?pair=XBTUSD&count=N
class KrakenBookFetcher : public IOrderbookFetcher {
public:
    std::string name() const override { return "Kraken"; }
    Orderbook   fetch(const std::string& symbol, int depth) const override;
private:
    static std::string to_kraken_symbol(const std::string& sym);
};

// Simulate VWAP fill: buy_side=true walks asks, false walks bids.
FillSim simulate_fill(const Orderbook& book, double quote_usd, bool buy_side);

void run_analysis(const std::vector<IOrderbookFetcher*>& exchanges,
                  const std::string&         symbol,
                  int                        depth,
                  const std::vector<double>& amounts,
                  const std::string&         output_file);