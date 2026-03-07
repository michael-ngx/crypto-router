#include "master_feed.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace {
constexpr std::int64_t kTransportStaleNs = 10'000'000'000; // 10 seconds
constexpr std::int64_t kQuietBookNs = 5'000'000'000;       // 5 seconds

std::int64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
}

void UIMasterFeed::add_feed(std::shared_ptr<IVenueFeed> feed) {
    if (!feed) return;
    if (feed->canonical() != canonical_) {
        // In production, you might want to throw or log.
        return;
    }
    std::lock_guard<std::mutex> lk(m_);
    feeds_.push_back(std::move(feed));
}

UIConsolidated UIMasterFeed::snapshot_consolidated(std::size_t depth) const {
    UIConsolidated out;
    out.symbol = canonical_;

    struct FeedState {
        std::string venue;
        std::shared_ptr<const BookSnapshot> snapshot;
        std::int64_t last_transport_ns{0};
        std::int64_t last_book_update_ns{0};
    };

    // Take a snapshot of all venues and liveness metadata.
    std::vector<FeedState> states;
    {
        std::lock_guard<std::mutex> lk(m_);
        states.reserve(feeds_.size());
        for (auto& f : feeds_) {
            states.push_back(FeedState{
                f->venue(),
                f->load_snapshot(),          // atomic book snapshot
                f->last_transport_ns(),      // monotonic transport liveness
                f->last_book_update_ns(),    // monotonic book update recency
            });
        }
    }

    // Keep explicit venue coverage independent from which venue contributes top levels.
    std::unordered_set<std::string> seen_venues;
    for (const auto& state : states) {
        if (seen_venues.insert(state.venue).second) {
            out.venues.push_back(state.venue);
        }
    }
    std::sort(out.venues.begin(), out.venues.end());

    const auto now = now_ns();
    std::vector<std::shared_ptr<const BookSnapshot>> connected_snapshots;
    connected_snapshots.reserve(states.size());
    bool has_connected_transport = false;
    bool has_seen_transport = false;
    bool has_recent_book_update = false;

    // Keep feeds with active transport; mark "quiet" if transport is alive but
    // no recent book updates have arrived.
    for (const auto& state : states) {
        if (state.last_transport_ns > 0) {
            has_seen_transport = true;
        }
        if (state.last_transport_ns <= 0) continue;
        if (now - state.last_transport_ns > kTransportStaleNs) continue;

        has_connected_transport = true;
        if (state.last_book_update_ns > 0 &&
            now - state.last_book_update_ns <= kQuietBookNs) {
            has_recent_book_update = true;
        }

        auto& snapshot = state.snapshot;
        if (!snapshot) continue;
        if (snapshot->ts_ns <= 0) continue;

        connected_snapshots.push_back(snapshot);
        if (snapshot->ts_ms > out.last_updated_ms) {
            out.last_updated_ms = snapshot->ts_ms;
        }
    }

    if (!has_connected_transport) {
        if (!has_seen_transport) {
            out.is_warming = true;
            return out;
        }
        out.is_cold = true;
        return out;
    }

    out.is_quiet = !has_recent_book_update;

    if (connected_snapshots.empty()) {
        return out;
    }

    // Flatten all per-venue ladders into a single list with venue info.
    std::vector<UILadderLevel> all_bids;
    std::vector<UILadderLevel> all_asks;
    all_bids.reserve(connected_snapshots.size() * depth);
    all_asks.reserve(connected_snapshots.size() * depth);

    for (auto& snapshot : connected_snapshots) {
        std::size_t taken_bids = 0;
        for (const auto& lvl : snapshot->bids) {
            if (taken_bids++ >= depth) break;
            all_bids.push_back(UILadderLevel{snapshot->venue, lvl.price, lvl.size});
        }
        std::size_t taken_asks = 0;
        for (const auto& lvl : snapshot->asks) {
            if (taken_asks++ >= depth) break;
            all_asks.push_back(UILadderLevel{snapshot->venue, lvl.price, lvl.size});
        }
    }

    // Sort and trim to desired depth.
    auto sort_and_trim = [depth](auto& v, bool bids_side) {
        if (v.empty()) return;

        if (bids_side) {
            // Bids: highest price first; tie-breaker by larger size.
            std::sort(v.begin(), v.end(),
                      [](const UILadderLevel& a, const UILadderLevel& b) {
                          if (a.price != b.price) return a.price > b.price;
                          return a.size > b.size;
                      });
        } else {
            // Asks: lowest price first; tie-breaker by larger size.
            std::sort(v.begin(), v.end(),
                      [](const UILadderLevel& a, const UILadderLevel& b) {
                          if (a.price != b.price) return a.price < b.price;
                          return a.size > b.size;
                      });
        }

        if (v.size() > depth) {
            v.resize(depth);
        }
    };

    sort_and_trim(all_bids, true);
    sort_and_trim(all_asks, false);

    out.bids = std::move(all_bids);
    out.asks = std::move(all_asks);

    return out;
}
