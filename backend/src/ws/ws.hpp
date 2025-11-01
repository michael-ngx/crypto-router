#pragma once
#include <functional>
#include <string>

// Interface for a market-data WebSocket connector.
// start() spawns an internal I/O thread and returns immediately
// stop() requests a graceful close and joins the internal thread.
// OnMsg(json): called for each text frame from the exchange
struct IMarketWs
{
    using OnMsg = std::function<void(const std::string &)>;
    virtual ~IMarketWs() = default;
    virtual void start(unsigned short port = 443) = 0;
    virtual void stop() = 0;
};

// NOTE: Each exchange connection implementation has pointer to Implementation (PIMPL) to hide Boost headers from dependents


class CoinbaseWs : public IMarketWs
{
public:
    CoinbaseWs(std::string product_id, OnMsg cb);
    ~CoinbaseWs();
    // Non-copyable, movable
    CoinbaseWs(const CoinbaseWs &) = delete;
    CoinbaseWs &operator=(const CoinbaseWs &) = delete;
    CoinbaseWs(CoinbaseWs &&) noexcept = default;
    CoinbaseWs &operator=(CoinbaseWs &&) noexcept = default;

    void start(unsigned short port = 443) override; // returns immediately
    void stop() noexcept override;                  // graceful shutdown

private:
    struct Impl;
    Impl *impl_;
};

class KrakenWs : public IMarketWs
{
public:
    // symbol like "BTC/USD"; event_trigger = "trades" or "bbo"
    KrakenWs(std::string symbol, OnMsg cb, std::string event_trigger = "trades");
    ~KrakenWs();
    // Non-copyable, movable
    KrakenWs(const KrakenWs &) = delete;
    KrakenWs &operator=(const KrakenWs &) = delete;
    KrakenWs(KrakenWs &&) noexcept = default;
    KrakenWs &operator=(KrakenWs &&) noexcept = default;

    void start(unsigned short port = 443) override; // returns immediately
    void stop() noexcept override;                  // graceful shutdown

private:
    struct Impl;
    Impl *impl_;
};