#pragma once
#include <functional>
#include <string>

// A minimal interface for a market-data WebSocket connector.
// start(): runs the IO loop on the current thread until stop() or error
// on_message(json): called for each text frame from the exchange
class CoinbaseWs {
public:
    using OnMsg = std::function<void(const std::string&)>;

    CoinbaseWs(std::string product_id, OnMsg cb);
    ~CoinbaseWs();

    void start(unsigned short port = 443); // blocks run loop
    void stop();

private:
    struct Impl; // Pointer to Implementation (PIMPL) to hide Boost headers from dependents
    Impl* impl_;
};