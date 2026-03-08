#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include "router/router_common.hpp"

struct VenueFeePolicy {
    double maker{0.0};
    double taker{0.0};
};

inline constexpr double kCoinbaseMakerFeeRate = 0.0040; // 40 bps
inline constexpr double kCoinbaseTakerFeeRate = 0.0060; // 60 bps
inline constexpr double kKrakenMakerFeeRate = 0.0025;   // 25 bps
inline constexpr double kKrakenTakerFeeRate = 0.0040;   // 40 bps

inline VenueFeePolicy fee_policy_for_venue(std::string_view venue) {
    if (venue == "Coinbase") {
        return VenueFeePolicy{kCoinbaseMakerFeeRate, kCoinbaseTakerFeeRate};
    }
    if (venue == "Kraken") {
        return VenueFeePolicy{kKrakenMakerFeeRate, kKrakenTakerFeeRate};
    }
    return VenueFeePolicy{};
}

inline double fee_rate_for_order(std::string_view venue, bool use_maker_fee) {
    const VenueFeePolicy policy = fee_policy_for_venue(venue);
    return use_maker_fee ? policy.maker : policy.taker;
}

inline double fee_adjusted_price(double raw_price, bool is_buy, double fee_rate) {
    return is_buy
        ? raw_price * (1.0 + fee_rate)
        : raw_price * (1.0 - fee_rate);
}

// Fee-aware best-price strategy:
// - market orders use taker fees
// - limit orders use maker fees
struct RouterV2BestPriceFee {
public:
    RoutingDecision route_order(
        const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
        const std::string& side_lower,
        double quantity,
        const std::optional<double>& limit_price) const
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

        const bool use_maker_fee = limit_price.has_value();

        struct SnapshotCursor {
            const std::string* venue{nullptr};
            const std::vector<BookSnapshotLevel>* levels{nullptr};
            std::uint64_t seq{0};
            std::size_t idx{0};
            double fee_rate{0.0};

            bool valid() const noexcept {
                return levels != nullptr && idx < levels->size();
            }

            double price() const noexcept {
                if (!valid()) return 0.0;
                return (*levels)[idx].price;
            }

            double size() const noexcept {
                if (!valid()) return 0.0;
                return (*levels)[idx].size;
            }

            void next() noexcept {
                if (!valid()) return;
                ++idx;
            }
        };

        // Hold shared_ptr lifetime while routing.
        std::vector<std::shared_ptr<const BookSnapshot>> snapshots;
        snapshots.reserve(feeds.size());

        std::vector<SnapshotCursor> snapshot_cursors;
        snapshot_cursors.reserve(feeds.size());

        for (const auto& feed : feeds) {
            if (!feed) continue;

            auto snapshot = feed->load_snapshot();
            if (!snapshot) continue;

            const auto& side = is_buy ? snapshot->asks : snapshot->bids;
            if (side.empty()) continue;

            snapshots.push_back(std::move(snapshot));
            const auto& held_snapshot = snapshots.back();

            snapshot_cursors.push_back(
                SnapshotCursor{
                    &held_snapshot->venue,
                    &(is_buy ? held_snapshot->asks : held_snapshot->bids),
                    held_snapshot->seq,
                    0,
                    fee_rate_for_order(held_snapshot->venue, use_maker_fee)
                }
            );
        }

        if (snapshot_cursors.empty()) {
            out.message = "no liquidity available";
            return out;
        }

        struct HeapNode {
            std::size_t venue_idx{0};
            double price{0.0};
            double effective_price{0.0};
            double size{0.0};
            std::uint64_t seq{0};
        };
        struct HeapCompare {
            bool is_buy{true};
            bool operator()(const HeapNode& a, const HeapNode& b) const {
                if (a.effective_price != b.effective_price) {
                    // For buys: lower fee-adjusted asks are better.
                    // For sells: higher fee-adjusted bids are better.
                    return is_buy
                        ? a.effective_price > b.effective_price
                        : a.effective_price < b.effective_price;
                }
                if (a.price != b.price) {
                    // Fall back to raw price for deterministic tie-breaks.
                    return is_buy ? a.price > b.price : a.price < b.price;
                }
                // Prefer larger resting size for tie-break.
                if (a.size != b.size) return a.size < b.size;
                // Tie-break with fresher published snapshot.
                return a.seq < b.seq;
            }
        };

        std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap(
            HeapCompare{is_buy}
        );

        for (std::size_t i = 0; i < snapshot_cursors.size(); ++i) {
            const auto& cursor = snapshot_cursors[i];
            const double price = cursor.price();
            heap.push(
                HeapNode{
                    i,
                    price,
                    fee_adjusted_price(price, is_buy, cursor.fee_rate),
                    cursor.size(),
                    cursor.seq
                }
            );
        }

        if (heap.empty()) {
            out.message = "no liquidity available";
            return out;
        }

        double remaining = quantity;
        double total_notional = 0.0;

        // Aggregate by venue index directly (faster than hashing by venue string).
        std::vector<double> venue_qty(snapshot_cursors.size(), 0.0);
        std::vector<double> venue_notional(snapshot_cursors.size(), 0.0);
        std::vector<std::size_t> touched_venues;
        touched_venues.reserve(snapshot_cursors.size());

        while (remaining > kRoutingEps && !heap.empty()) {
            const HeapNode lvl = heap.top();
            heap.pop();

            if (limit_price.has_value()) {
                if (is_buy && lvl.price > *limit_price) break;
                if (is_sell && lvl.price < *limit_price) break;
            }

            const double take_qty = std::min(remaining, lvl.size);
            if (take_qty <= kRoutingEps) continue;

            if (venue_qty[lvl.venue_idx] <= kRoutingEps) {
                touched_venues.push_back(lvl.venue_idx);
            }
            venue_qty[lvl.venue_idx] += take_qty;
            venue_notional[lvl.venue_idx] += (take_qty * lvl.price);

            remaining -= take_qty;
            total_notional += (take_qty * lvl.price);

            auto& cursor = snapshot_cursors[lvl.venue_idx];
            cursor.next();
            if (cursor.valid()) {
                const double next_price = cursor.price();
                heap.push(
                    HeapNode{
                        lvl.venue_idx,
                        next_price,
                        fee_adjusted_price(next_price, is_buy, cursor.fee_rate),
                        cursor.size(),
                        cursor.seq
                    }
                );
            }
        }

        out.routable_qty = quantity - remaining;
        if (out.routable_qty > kRoutingEps) {
            out.indicative_average_price = total_notional / out.routable_qty;
        }
        out.fully_routable = remaining <= kRoutingEps;

        out.slices.reserve(touched_venues.size());
        for (const auto idx : touched_venues) {
            const double q = venue_qty[idx];
            if (q <= kRoutingEps) continue;
            out.slices.push_back(
                RouteSlice{
                    *snapshot_cursors[idx].venue,
                    q,
                    venue_notional[idx] / q
                }
            );
        }

        set_routing_message(out, limit_price);
        return out;
    }
};
