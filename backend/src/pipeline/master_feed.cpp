#include "master_feed.hpp"
#include <algorithm>

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

    // Take a snapshot of all venues.
    std::vector<std::shared_ptr<const TopSnapshot>> snaps;
    {
        std::lock_guard<std::mutex> lk(m_);
        snaps.reserve(feeds_.size());
        for (auto& f : feeds_) {
            snaps.emplace_back(f->load_top()); // atomic load from each venue
        }
    }

    // Build per-venue map.
    for (auto& sp : snaps) {
        if (!sp) continue;
        out.per_venue.emplace(sp->venue, sp);
    }

    // Flatten all per-venue ladders into a single list with venue info.
    std::vector<UILadderLevel> all_bids;
    std::vector<UILadderLevel> all_asks;
    all_bids.reserve(snaps.size() * depth);
    all_asks.reserve(snaps.size() * depth);

    for (auto& sp : snaps) {
        if (!sp) continue;

        for (const auto& [px, sz] : sp->bids) {
            all_bids.push_back(UILadderLevel{sp->venue, px, sz});
        }
        for (const auto& [px, sz] : sp->asks) {
            all_asks.push_back(UILadderLevel{sp->venue, px, sz});
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