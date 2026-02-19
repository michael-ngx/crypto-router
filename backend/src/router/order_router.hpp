#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "md/venue_feed_iface.hpp"

struct RouteSlice {
    std::string venue;
    double quantity{0.0};
    double price{0.0};
};

struct RoutingDecision {
    bool fully_routable{false};
    double requested_qty{0.0};
    double routable_qty{0.0};
    double indicative_average_price{0.0};
    std::vector<RouteSlice> slices;
    std::string message;
};

// Computes the best venue split for an order from full per-venue books.
// Complexity: O(K log V) where V is number of venues and K is total depth consumed until fill across venues
inline RoutingDecision route_order_from_books(
    const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
    const std::string& side_lower,
    double quantity,
    const std::optional<double>& limit_price)
{
    RoutingDecision out;
    out.requested_qty = quantity;

    if (quantity <= 0.0) {
        out.message = "invalid quantity";
        return out;
    }

    const bool is_buy = side_lower == "buy";
    const bool is_sell = side_lower == "sell";
    if (!is_buy && !is_sell) {
        out.message = "invalid side";
        return out;
    }

    struct VenueCursor {
        std::string venue;
        Book::LevelCursor cursor;
    };


    // Fill vector of venue cursors
    std::vector<VenueCursor> venue_cursors;
    venue_cursors.reserve(feeds.size());

    for (const auto& feed : feeds) {
        if (!feed) continue;

        const auto& book = feed->book();
        Book::LevelCursor cursor = is_buy ? book.ask_cursor() : book.bid_cursor();
        if (!cursor.valid()) continue;

        venue_cursors.push_back(VenueCursor{feed->venue(), std::move(cursor)});
    }

    if (venue_cursors.empty()) {
        out.message = "no liquidity available";
        return out;
    }

    constexpr double kEps = 1e-12;

    struct HeapNode {
        std::size_t venue_idx{0};
        double price{0.0};
        double size{0.0};
    };
    struct HeapCompare {
        bool is_buy{true};
        bool operator()(const HeapNode& a, const HeapNode& b) const {
            if (a.price != b.price) {
                // For buys: lower asks are better. For sells: higher bids are better.
                return is_buy ? a.price > b.price : a.price < b.price;
            }
            // Prefer larger resting size for tie-break.
            return a.size < b.size;
        }
    };

    // Create a min-heap for buys (best price = lowest ask) or max-heap for sells (best price = highest bid)
    std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap(
        HeapCompare{is_buy}
    );

    // Initialize heap with the top of each venue's book
    for (std::size_t i = 0; i < venue_cursors.size(); ++i) {
        auto& c = venue_cursors[i].cursor;
        heap.push(HeapNode{i, c.price(), c.size()});
    }

    if (heap.empty()) {
        out.message = "no liquidity available";
        return out;
    }

    double remaining = quantity;
    double total_notional = 0.0;

    while (remaining > kEps && !heap.empty()) {
        HeapNode lvl = heap.top();
        heap.pop();

        if (limit_price.has_value()) {
            if (is_buy && lvl.price > *limit_price) break;
            if (is_sell && lvl.price < *limit_price) break;
        }

        const double take_qty = std::min(remaining, lvl.size);
        if (take_qty <= kEps) continue;

        if (!out.slices.empty() &&
            out.slices.back().venue == venue_cursors[lvl.venue_idx].venue &&
            std::fabs(out.slices.back().price - lvl.price) <= kEps) {
            out.slices.back().quantity += take_qty;
        } else {
            out.slices.push_back(
                RouteSlice{venue_cursors[lvl.venue_idx].venue, take_qty, lvl.price}
            );
        }

        remaining -= take_qty;
        total_notional += (take_qty * lvl.price);

        auto& src = venue_cursors[lvl.venue_idx].cursor;
        src.next();
        if (src.valid()) {
            heap.push(HeapNode{lvl.venue_idx, src.price(), src.size()});
        }
    }

    out.routable_qty = quantity - remaining;
    if (out.routable_qty > kEps) {
        out.indicative_average_price = total_notional / out.routable_qty;
    }
    out.fully_routable = remaining <= kEps;

    if (out.routable_qty <= kEps) {
        out.message = limit_price.has_value()
            ? "no liquidity matched the limit price"
            : "no liquidity available";
    } else if (out.fully_routable) {
        out.message = "fully routable from current books";
    } else {
        out.message = limit_price.has_value()
            ? "partially routable: limit-constrained liquidity"
            : "partially routable: insufficient liquidity";
    }

    return out;
}
