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
#include "venue_feed_iface.hpp"
#include "book.hpp"
#include "book_events.hpp"
#include "symbol_codec.hpp"
#include "top_snapshot.hpp"

// Backpressure policy when the queue is full
enum class Backpressure {
    DropNewest,   // drop newest frame
    DropOldest,   // evict one stale, then push newest
    SignalResync  // set a flag for re-snapshot
};

// Maximum depth of top-N snapshots to publish
constexpr std::size_t MAX_TOP_DEPTH = 50;

// VenueFeed is parameterized by concrete Ws type and concrete Parser type.
// Each VenueFeed owns:
//  - a WS connector (producer thread lives in ws_thread_)
//  - an SPSC ring for raw messages
//  - a consumer thread that parses & applies to
//  - a per-venue full-depth Book
template <typename WsT, typename ParserT, std::size_t QueuePow2 = 4096>
class VenueFeed final : public IVenueFeed {
public:
    VenueFeed(std::string venue_name,
              std::string canonical_symbol,
              Backpressure bp = Backpressure::DropOldest,
              std::size_t top_depth = MAX_TOP_DEPTH)
    : venue_(std::move(venue_name))
    , canonical_(std::move(canonical_symbol))
    , backpressure_(bp)
    , running_(false)
    , top_depth_(top_depth)
    , book_(venue_, canonical_) {}

    // Start: constructs WS with an enqueue-only callback and starts consumer thread.
    // `venue_symbol` must be formatted for the venue (use SymbolCodec::to_venue).
    void start_ws(const std::string& venue_symbol, unsigned short port = 443) override {
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

    // Orderly stop consumer + websocket
    void stop() override {
        running_.store(false, std::memory_order_relaxed);
        if (ws_) ws_->stop();
        if (ws_thread_.joinable()) ws_thread_.join();
        if (consumer_.joinable()) consumer_.join();
    }

    // Atomic load for UI/router readers (lock-free)
    std::shared_ptr<const TopSnapshot> load_top() const noexcept override {
        return std::atomic_load_explicit(&top_, std::memory_order_acquire);
    }
    
    // Identity
    const std::string& venue() const override     { return venue_; }
    const std::string& canonical() const override { return canonical_; }

    // Access to Book
    Book& book() override { return book_; }
    const Book& book() const override { return book_; }

private:
    static std::int64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static std::int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    // Capture and publish a fresh TopSnapshot (immutable)
    void publish_top(std::size_t depth) {
        TopSnapshot tmp;
        tmp.venue  = venue_;
        tmp.symbol = canonical_;
        tmp.ts_ns  = now_ns();
        tmp.ts_ms  = now_ms();
        tmp.bids   = book_.top_bids(depth);
        tmp.asks   = book_.top_asks(depth);

        // construct the shared_ptr holding a const TopSnapshot
        auto snap = std::make_shared<const TopSnapshot>(std::move(tmp));
        std::atomic_store_explicit(&top_, std::move(snap), std::memory_order_release);
    }


    /*
     * Main consumer loop: try_pop from queue, parse, apply to book, publish top-N snapshot.
    */
    void consume_loop() {
        ParserT parser;
        std::vector<BookEvent> evs;
        std::string raw;

        publish_top(top_depth_); // initial empty snapshot

        while (running_.load(std::memory_order_relaxed)) {
            if (!queue_.try_pop(raw)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            evs.clear();
            // parser should parse full events; no depth limit here
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
                publish_top(top_depth_); // publish after every applied batch
            }
        }

        // optional drain on shutdown
        while (queue_.try_pop(raw)) {
            evs.clear();
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
                publish_top(top_depth_);
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

    // Published Top-N view (immutable via shared_ptr)
    std::shared_ptr<const TopSnapshot> top_{nullptr};
    std::size_t top_depth_; // requested published depth

    Book book_;
};
