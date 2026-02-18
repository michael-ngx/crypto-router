#pragma once

#include <string>

#include "venues/market_ws.hpp"

// NOTE: Each exchange connection implementation uses a PIMPL to hide Boost headers.
class CoinbaseWs : public IMarketWs {
public:
    CoinbaseWs(std::string product_id, OnMsg cb);
    ~CoinbaseWs();
    CoinbaseWs(const CoinbaseWs &) = delete;
    CoinbaseWs &operator=(const CoinbaseWs &) = delete;
    CoinbaseWs(CoinbaseWs &&) noexcept = default;
    CoinbaseWs &operator=(CoinbaseWs &&) noexcept = default;

    void start(unsigned short port = 443) override;
    void stop() noexcept override;

private:
    struct Impl;
    Impl *impl_;
};
