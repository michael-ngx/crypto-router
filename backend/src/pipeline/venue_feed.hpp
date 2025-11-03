#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

#include "util/spsc_ring.hpp"
#include "ws/ws.hpp"
#include "md/book.hpp"
#include "md/book_events.hpp"
#include "md/symbol_codec.hpp"

// Backpressure policy when the queue is full
enum class Backpressure {
    DropNewest,   // drop newest frame
    DropOldest,   // evict one stale, then push newest
    SignalResync  // set a flag for re-snapshot
};

// VenueFeed is parameterized by concrete Ws type and concrete Parser type.
// Each VenueFeed owns:
//  - a WS connector (producer thread lives in ws_thread_)
//  - an SPSC ring for raw messages
//  - a consumer thread that parses & applies to a per-venue full-depth Book
template <typename WsT, typename ParserT, std::size_t QueuePow2 = 4096>
class VenueFeed {
public:
    VenueFeed(std::string venue_name,
              std::string canonical_symbol,
              Backpressure bp = Backpressure::DropOldest)
    : venue_(std::move(venue_name))
    , canonical_(std::move(canonical_symbol))
    , backpressure_(bp)
    , running_(false)
    , book_(venue_, canonical_)        // full-depth book
    {}

    // Start: constructs WS with an enqueue-only callback and starts consumer thread.
    // `venue_symbol` must be formatted for the venue (use SymbolCodec::to_venue).
    void start_ws(const std::string& venue_symbol, unsigned short port = 443) {
        // Build WS with a lightweight callback that only enqueues strings
        ws_ = std::make_unique<WsT>(venue_symbol, [this](const std::string& raw){
            std::string msg(raw);
            if (!queue_.try_push(std::move(msg))) {
                switch (backpressure_) {
                    case Backpressure::DropNewest:
                        // drop newest
                        break;
                    case Backpressure::DropOldest: {
                        std::string trash;
                        (void)queue_.try_pop(trash);
                        (void)queue_.try_push(std::move(msg));
                        break;
                    }
                    case Backpressure::SignalResync:
                        // TODO: mark a flag to trigger REST resnapshot
                        break;
                }
            }
        });

        // Start consumer thread (one per venue)
        running_.store(true, std::memory_order_relaxed);
        consumer_ = std::thread([this] { consume_loop(); });

        // Start the WS on its own thread
        ws_thread_ = std::thread([this, port]{
            ws_->start(port);
        });
    }

    // Stop: orderly stop consumer + websocket
    void stop() {
        running_.store(false, std::memory_order_relaxed);
        if (ws_) ws_->stop();
        if (ws_thread_.joinable()) ws_thread_.join();
        if (consumer_.joinable()) consumer_.join();
    }

    Book& book() noexcept { return book_; }
    const Book& book() const noexcept { return book_; }

private:
    void consume_loop() {
        ParserT parser;
        std::vector<BookEvent> evs;
        std::string raw;

        while (running_.load(std::memory_order_relaxed)) {
            if (!queue_.try_pop(raw)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            evs.clear();
            // parser should parse full events; no depth limit here
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
            }
        }

        // optional drain on shutdown
        while (queue_.try_pop(raw)) {
            evs.clear();
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
            }
        }
    }

    // Identity
    std::string venue_;
    std::string canonical_;
    Backpressure backpressure_;

    // Per-venue components
    SpscRing<std::string, QueuePow2> queue_;
    std::unique_ptr<WsT> ws_;
    std::thread ws_thread_;
    std::thread consumer_;
    std::atomic<bool> running_;

    Book book_;
};