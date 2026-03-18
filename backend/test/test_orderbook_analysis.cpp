#include "orderbook_analysis.hpp"
#include <iostream>
#include <sstream>

static std::vector<double> parse_amounts(const std::string& s) {
    std::vector<double> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ','))
        try { out.push_back(std::stod(tok)); } catch (...) {}
    return out;
}

int main(int argc, char* argv[]) {
    std::string         symbol  = "BTC-USD";
    int                 depth   = 50;
    std::vector<double> amounts = {100.0, 1000.0, 10000.0, 100000.0};

    if (argc >= 2) symbol  = argv[1];
    if (argc >= 3) depth   = std::stoi(argv[2]);
    if (argc >= 4) { auto a = parse_amounts(argv[3]); if (!a.empty()) amounts = a; }

    std::cout << "Symbol: " << symbol << "  Depth: " << depth << "  Amounts:";
    for (double a : amounts) std::cout << " $" << a;
    std::cout << "\n\n";

    CoinbaseBookFetcher coinbase;
    KrakenBookFetcher   kraken;

    std::vector<IOrderbookFetcher*> fetchers = {
        &coinbase,
        &kraken,
        // &new_exchange,
    };

    std::string safe = symbol;
    for (char& c : safe) if (c == '/' || c == '-') c = '_';
    std::string out = "orderbook_" + safe + "_d" + std::to_string(depth) + ".txt";

    run_analysis(fetchers, symbol, depth, amounts, out);
    return 0;
}