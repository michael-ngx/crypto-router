#pragma once

#include <string>

#include "venues/market_ws.hpp"

// NOTE: Each exchange connection implementation uses a PIMPL to hide Boost headers.
class KrakenWs : public IMarketWs {
public:
    // symbol like "BTC/USD"; event_trigger = "trades" or "bbo"
    KrakenWs(std::string symbol, OnMsg cb, std::string event_trigger = "trades");
    ~KrakenWs();
    KrakenWs(const KrakenWs &) = delete;
    KrakenWs &operator=(const KrakenWs &) = delete;
    KrakenWs(KrakenWs &&) noexcept = default;
    KrakenWs &operator=(KrakenWs &&) noexcept = default;

    void start(unsigned short port = 443) override;
    void stop() noexcept override;

private:
    struct Impl;
    Impl *impl_;
};
