#pragma once
#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <chrono>

#include "util/spsc_ring.hpp"
#include "venue_feed_iface.hpp"
#include "book.hpp"
#include "book_events.hpp"
#include "book_snapshot.hpp"

// Backpressure policy when the queue is full
enum class Backpressure {
    DropNewest,   // drop newest frame
    DropOldest,   // evict one stale, then push newest
    SignalResync  // set a flag for re-snapshot
};

// Adaptive gate for immutable snapshot publication.
// Book writes still happen for every market-data update.
struct PublishPolicy {
    std::int64_t min_publish_interval_ns{500'000}; // 0.5ms max staleness target under load
    std::uint32_t max_updates_per_publish{32};     // burst guard for deep-book churn
    double top_size_rel_change_trigger{0.05};      // 5% top-size change trigger
};

// VenueFeed is parameterized by concrete Ws type and concrete Parser type.
// Each VenueFeed owns:
//  - a WS connector (producer thread lives in ws_thread_)
//  - an SPSC ring for raw messages
//  - a single consumer thread that parses and mutates the book
//  - immutable snapshots published atomically for UI and router readers
template <typename WsT, typename ParserT, std::size_t QueuePow2 = 4096>
class VenueFeed final : public IVenueFeed {
public:
    VenueFeed(std::string venue_name,
              std::string canonical_symbol,
              Backpressure bp = Backpressure::DropOldest,
              PublishPolicy publish_policy = PublishPolicy{})
    : venue_(std::move(venue_name))
    , canonical_(std::move(canonical_symbol))
    , backpressure_(bp)
    , publish_policy_(publish_policy)
    , running_(false)
    , book_(venue_, canonical_) {}

    // Start: constructs WS with an enqueue-only callback and starts consumer thread.
    // `venue_symbol` must be formatted for the venue (use SymbolCodec::to_venue).
    void start_ws(const std::string& venue_symbol, unsigned short port = 443) override {
        // Build WS with a lightweight callback that only enqueues strings
        ws_ = std::make_unique<WsT>(venue_symbol, [this](const std::string& raw){
            last_transport_ns_.store(now_ns(), std::memory_order_release);
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

    std::shared_ptr<const BookSnapshot> load_snapshot() const noexcept override {
        return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    }

    std::int64_t last_transport_ns() const noexcept override {
        return last_transport_ns_.load(std::memory_order_acquire);
    }

    std::int64_t last_book_update_ns() const noexcept override {
        return last_book_update_ns_.load(std::memory_order_acquire);
    }
    
    // Identity
    const std::string& venue() const override     { return venue_; }
    const std::string& canonical() const override { return canonical_; }

private:
    static std::int64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static std::int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    static bool materially_changed(
        const std::optional<std::pair<double, double>>& prev,
        const std::optional<std::pair<double, double>>& cur,
        double size_rel_threshold) noexcept {
        if (prev.has_value() != cur.has_value()) return true;
        if (!prev.has_value()) return false;

        constexpr double kPriceEps = 1e-12;
        if (std::abs(prev->first - cur->first) > kPriceEps) return true;

        const double base = std::max(std::abs(prev->second), kPriceEps);
        const double rel = std::abs(cur->second - prev->second) / base;
        return rel >= size_rel_threshold;
    }

    bool should_publish_after_update(std::int64_t ts_ns) {
        ++pending_updates_since_publish_;

        const bool age_due =
            publish_policy_.min_publish_interval_ns <= 0 ||
            (ts_ns - last_publish_ns_) >= publish_policy_.min_publish_interval_ns;

        const bool burst_due =
            publish_policy_.max_updates_per_publish > 0 &&
            pending_updates_since_publish_ >= publish_policy_.max_updates_per_publish;

        const auto best_bid = book_.best_bid();
        const auto best_ask = book_.best_ask();
        const bool top_due =
            materially_changed(last_published_best_bid_, best_bid,
                               publish_policy_.top_size_rel_change_trigger) ||
            materially_changed(last_published_best_ask_, best_ask,
                               publish_policy_.top_size_rel_change_trigger);

        return age_due || burst_due || top_due;
    }

    void publish_snapshot(std::int64_t ts_ns) {
        const std::int64_t ts_ms = now_ms();
        const std::uint64_t seq =
            published_seq_.fetch_add(1, std::memory_order_relaxed) + 1;

        BookSnapshot snapshot_tmp;
        snapshot_tmp.venue  = venue_;
        snapshot_tmp.symbol = canonical_;
        snapshot_tmp.seq    = seq;
        snapshot_tmp.ts_ns  = ts_ns;
        snapshot_tmp.ts_ms  = ts_ms;
        book_.copy_snapshot_levels(snapshot_tmp.bids, snapshot_tmp.asks);

        auto snapshot_ptr = std::make_shared<const BookSnapshot>(std::move(snapshot_tmp));
        std::atomic_store_explicit(&snapshot_, std::move(snapshot_ptr), std::memory_order_release);

        last_publish_ns_ = ts_ns;
        pending_updates_since_publish_ = 0;
        last_published_best_bid_ = book_.best_bid();
        last_published_best_ask_ = book_.best_ask();
    }


    /*
     * Main consumer loop: try_pop from queue, parse, apply to book,
     * then publish immutable snapshots for readers.
    */
    void consume_loop() {
        ParserT parser;
        std::vector<BookEvent> evs;
        std::string raw;

        publish_snapshot(now_ns()); // initial empty snapshot

        while (running_.load(std::memory_order_relaxed)) {
            if (!queue_.try_pop(raw)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            evs.clear();
            // parser should parse full events; no depth limit here
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
                const auto ts_ns = now_ns();
                last_book_update_ns_.store(ts_ns, std::memory_order_release);
                if (should_publish_after_update(ts_ns)) {
                    publish_snapshot(ts_ns);
                }
            }
        }

        // optional drain on shutdown
        while (queue_.try_pop(raw)) {
            evs.clear();
            if (parser.parse(raw, evs)) {
                book_.apply_many(evs);
                const auto ts_ns = now_ns();
                last_book_update_ns_.store(ts_ns, std::memory_order_release);
                if (should_publish_after_update(ts_ns)) {
                    publish_snapshot(ts_ns);
                }
            }
        }
    }

    // Identity
    std::string venue_;
    std::string canonical_;
    Backpressure backpressure_;
    PublishPolicy publish_policy_;

    // Per-venue components
    SpscRing<std::string, QueuePow2> queue_;
    std::unique_ptr<WsT> ws_;
    std::thread ws_thread_;
    std::thread consumer_;
    std::atomic<bool> running_;

    // Published immutable views.
    std::shared_ptr<const BookSnapshot> snapshot_{nullptr};
    std::atomic<std::uint64_t> published_seq_{0};
    std::atomic<std::int64_t> last_transport_ns_{0};
    std::atomic<std::int64_t> last_book_update_ns_{0};
    std::uint32_t pending_updates_since_publish_{0};
    std::int64_t last_publish_ns_{0};
    std::optional<std::pair<double, double>> last_published_best_bid_;
    std::optional<std::pair<double, double>> last_published_best_ask_;

    Book book_;
};
