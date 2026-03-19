#pragma once

#include <string>

#include "venues/market_ws.hpp"

class OkxWs : public IMarketWs {
public:
    // instId in OKX format (matches canonical): e.g. "BTC-USDT"
    OkxWs(std::string inst_id, OnMsg cb);
    ~OkxWs();
    OkxWs(const OkxWs&) = delete;
    OkxWs& operator=(const OkxWs&) = delete;
    OkxWs(OkxWs&&) noexcept = default;
    OkxWs& operator=(OkxWs&&) noexcept = default;

    void start(unsigned short port = 443) override;
    void stop() noexcept override;

private:
    struct Impl;
    Impl* impl_;
};
