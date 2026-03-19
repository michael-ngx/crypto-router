#pragma once

#include <string>

#include "venues/market_ws.hpp"

class BinanceWs : public IMarketWs {
public:
    // symbol in Binance format (lowercase, no separator): e.g. "btcusdt"
    BinanceWs(std::string symbol, OnMsg cb);
    ~BinanceWs();
    BinanceWs(const BinanceWs&) = delete;
    BinanceWs& operator=(const BinanceWs&) = delete;
    BinanceWs(BinanceWs&&) noexcept = default;
    BinanceWs& operator=(BinanceWs&&) noexcept = default;

    void start(unsigned short port = 443) override;
    void stop() noexcept override;

private:
    struct Impl;
    Impl* impl_;
};
