#pragma once

#include <functional>
#include <string>

// Interface for a market-data WebSocket connector.
// start() spawns an internal I/O thread and returns immediately.
// stop() requests a graceful close and joins the internal thread.
// OnMsg(json): called for each text frame from the exchange.
struct IMarketWs {
    using OnMsg = std::function<void(const std::string &)>;
    virtual ~IMarketWs() = default;
    virtual void start(unsigned short port = 443) = 0;
    virtual void stop() = 0;
};
