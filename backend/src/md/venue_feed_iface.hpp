#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "book.hpp"
#include "top_snapshot.hpp"

struct IVenueFeed {
    virtual ~IVenueFeed() = default;
    virtual void start_ws(const std::string& venue_symbol, unsigned short port = 443) = 0;
    virtual void stop() = 0;

    virtual const std::string& venue() const = 0;     // "Coinbase", "Kraken"
    virtual const std::string& canonical() const = 0; // "BTC-USD"

    virtual Book& book() = 0;
    virtual const Book& book() const = 0;
    
    // Lock-free atomic read of this venue’s current published top-N.
    virtual std::shared_ptr<const TopSnapshot> load_top() const noexcept = 0;

    // Monotonic timestamps for feed liveness signals.
    virtual std::int64_t last_transport_ns() const noexcept = 0;
    virtual std::int64_t last_book_update_ns() const noexcept = 0;
};
