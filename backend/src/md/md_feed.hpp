// TODO: MarketFeed class not currently used

#pragma once
#include "../ws/ws.hpp" // IMarketWs
#include "md_normalizer.hpp"
#include "md_types.hpp"
#include <functional>
#include <memory>
#include <chrono>

/// @brief MarketFeed combines a WebSocket market data source with a normalizer
class MarketFeed
{
public:
    using OnTick = std::function<void(const NormalizedTick &)>;
    MarketFeed(std::unique_ptr<IMarketWs> ws,
               std::unique_ptr<IMarketNormalizer> norm,
               OnTick cb)
        : ws_(std::move(ws)), norm_(std::move(norm)), on_tick_(std::move(cb)) {}

    void start(unsigned short port = 443)
    {
        // Wrap the raw callback and forward normalized ticks
        auto raw_cb = [this](const std::string &raw)
        {
            NormalizedTick t{};
            if (norm_->parse_ticker(raw, t))
            {
                if (t.ts_ns == 0)
                {
                    using namespace std::chrono;
                    t.ts_ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
                }
                if (on_tick_)
                    on_tick_(t);
            }
        };
        // Replace the ws callback (requires your concrete Ws to expose a setter or accept cb in ctor)
        // If your Ws takes the callback in ctor, create it with raw_cb instead.
        ws_->start(port); // in your current design this blocks; you might run it on a thread
    }

    void stop() noexcept { ws_->stop(); }

private:
    std::unique_ptr<IMarketWs> ws_;
    std::unique_ptr<IMarketNormalizer> norm_;
    OnTick on_tick_;
};