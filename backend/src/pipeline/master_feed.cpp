#include "master_feed.hpp"
#include <queue>
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

static void aggregate_side_desc( // for bids: highest first
    std::vector<std::vector<std::pair<double,double>>>& ladders,
    std::size_t depth,
    std::vector<std::pair<double,double>>& out)
{
    struct Node { double px; double sz; std::size_t i; std::size_t j; };
    auto cmp = [](const Node& a, const Node& b){ return a.px < b.px; }; // max-heap by price
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

    for (std::size_t i = 0; i < ladders.size(); ++i) {
        if (!ladders[i].empty()) {
            pq.push(Node{ladders[i][0].first, ladders[i][0].second, i, 0});
        }
    }

    out.clear();
    out.reserve(depth);

    double cur_px = 0.0;
    double acc_sz = 0.0;
    bool have = false;

    while (!pq.empty() && out.size() < depth) {
        auto n = pq.top(); pq.pop();

        if (!have) {
            cur_px = n.px; acc_sz = n.sz; have = true;
        } else if (n.px == cur_px) {
            acc_sz += n.sz;
        } else {
            out.emplace_back(cur_px, acc_sz);
            if (out.size() >= depth) break;
            cur_px = n.px; acc_sz = n.sz;
        }

        if (n.j + 1 < ladders[n.i].size()) {
            auto& nxt = ladders[n.i][n.j + 1];
            pq.push(Node{nxt.first, nxt.second, n.i, n.j + 1});
        }
    }
    if (have && out.size() < depth) {
        out.emplace_back(cur_px, acc_sz);
    }
}

static void aggregate_side_asc( // for asks: lowest first
    std::vector<std::vector<std::pair<double,double>>>& ladders,
    std::size_t depth,
    std::vector<std::pair<double,double>>& out)
{
    struct Node { double px; double sz; std::size_t i; std::size_t j; };
    auto cmp = [](const Node& a, const Node& b){ return a.px > b.px; }; // min-heap by price
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

    for (std::size_t i = 0; i < ladders.size(); ++i) {
        if (!ladders[i].empty()) {
            pq.push(Node{ladders[i][0].first, ladders[i][0].second, i, 0});
        }
    }

    out.clear();
    out.reserve(depth);

    double cur_px = 0.0;
    double acc_sz = 0.0;
    bool have = false;

    while (!pq.empty() && out.size() < depth) {
        auto n = pq.top(); pq.pop();

        if (!have) {
            cur_px = n.px; acc_sz = n.sz; have = true;
        } else if (n.px == cur_px) {
            acc_sz += n.sz;
        } else {
            out.emplace_back(cur_px, acc_sz);
            if (out.size() >= depth) break;
            cur_px = n.px; acc_sz = n.sz;
        }

        if (n.j + 1 < ladders[n.i].size()) {
            auto& nxt = ladders[n.i][n.j + 1];
            pq.push(Node{nxt.first, nxt.second, n.i, n.j + 1});
        }
    }
    if (have && out.size() < depth) {
        out.emplace_back(cur_px, acc_sz);
    }
}

UIConsolidated UIMasterFeed::snapshot_consolidated(std::size_t depth) const {
    UIConsolidated out;
    out.symbol = canonical_;

    std::vector<std::shared_ptr<const TopSnapshot>> snaps;
    {
        std::lock_guard<std::mutex> lk(m_);
        snaps.reserve(feeds_.size());
        for (auto& f : feeds_) {
            snaps.emplace_back(f->load_top()); // atomic load from each venue
        }
    }

    // Build per-venue copies (optional for UI side-panels)
    for (auto& sp : snaps) {
        if (!sp) continue;
        out.per_venue.emplace(sp->venue, sp);
    }

    // Collect side ladders
    std::vector<std::vector<std::pair<double,double>>> all_bids;
    std::vector<std::vector<std::pair<double,double>>> all_asks;
    all_bids.reserve(snaps.size());
    all_asks.reserve(snaps.size());
    for (auto& sp : snaps) {
        if (!sp) continue;
        all_bids.push_back(sp->bids);
        all_asks.push_back(sp->asks);
    }

    aggregate_side_desc(all_bids, depth, out.bids);
    aggregate_side_asc(all_asks, depth, out.asks);

    return out;
}