#pragma once

#include "router/router_common.hpp"
#include "venues/venue_api.hpp"

inline double fee_adjusted_price(double raw_price, bool is_buy, double fee_rate) {
    return is_buy
        ? raw_price * (1.0 + fee_rate)
        : raw_price * (1.0 - fee_rate);
}

inline double maker_effective_limit_price(double limit_price, bool is_buy, double maker_fee) {
    return fee_adjusted_price(limit_price, is_buy, maker_fee);
}

// Fee-aware best-price strategy under fixed-per-order tier assumption:
// - Fee tier is determined BEFORE routing using current trailing volume.
// - Fee tier does NOT change during this parent order, even across multiple fills.
// - Market orders use taker fee.
// - Limit orders are two-phase:
//   1) immediate-crossing quantity uses taker fee
//   2) residual resting quantity uses maker fee at the limit price
struct RouterV2BestPriceFee {
private:
    struct VenueState {
        std::shared_ptr<const BookSnapshot> snapshot;
        const std::string* venue{nullptr};
        std::uint64_t seq{0};
        double maker_fee{0.0};
        double taker_fee{0.0};
    };

    struct GreedyBookResult {
        double filled_qty{0.0};
        double raw_notional{0.0};
        std::vector<double> venue_qty;
        std::vector<double> venue_notional;
    };

    static std::vector<VenueState> collect_venue_states(
        const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info)
    {
        std::vector<VenueState> states;
        states.reserve(feeds.size());

        for (const auto& feed : feeds) {
            if (!feed) continue;

            auto snapshot = feed->load_snapshot();
            if (!snapshot) continue;

            double maker_fee = 0.0;
            double taker_fee = 0.0;
            auto info_it = venue_static_info.find(snapshot->venue);
            if (info_it != venue_static_info.end()) {
                double trailing_volume_usd = 0.0;
                auto runtime_it = venue_runtime_info.find(snapshot->venue);
                if (runtime_it != venue_runtime_info.end()) {
                    trailing_volume_usd = std::max(0.0, runtime_it->second.trailing_volume_usd);
                }

                const auto tier = info_it->second.fees.tier_for_volume(trailing_volume_usd);
                maker_fee = tier.maker_fee;
                taker_fee = tier.taker_fee;
            }

            states.push_back(
                VenueState{
                    snapshot,
                    &snapshot->venue,
                    snapshot->seq,
                    maker_fee,
                    taker_fee
                }
            );
        }

        return states;
    }

    static GreedyBookResult route_immediate_greedy(
        const std::vector<VenueState>& states,
        bool is_buy,
        double quantity,
        const std::optional<double>& limit_price)
    {
        GreedyBookResult result;
        result.venue_qty.assign(states.size(), 0.0);
        result.venue_notional.assign(states.size(), 0.0);

        if (quantity <= kRoutingEps || states.empty()) return result;

        struct SnapshotCursor {
            const std::string* venue{nullptr};
            const std::vector<BookSnapshotLevel>* levels{nullptr};
            std::uint64_t seq{0};
            std::size_t idx{0};
            double taker_fee{0.0}; // fixed for this order

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

        std::vector<SnapshotCursor> cursors;
        cursors.reserve(states.size());
        std::vector<std::size_t> state_idx_by_cursor;
        state_idx_by_cursor.reserve(states.size());

        for (std::size_t i = 0; i < states.size(); ++i) {
            const auto& state = states[i];
            if (!state.snapshot) continue;

            const auto* levels = &(is_buy ? state.snapshot->asks : state.snapshot->bids);
            if (levels->empty()) continue;

            state_idx_by_cursor.push_back(i);
            cursors.push_back(
                SnapshotCursor{
                    state.venue,
                    levels,
                    state.seq,
                    0,
                    state.taker_fee
                }
            );
        }

        if (cursors.empty()) return result;

        struct HeapNode {
            std::size_t cursor_idx{0};
            double price{0.0};
            double effective_price{0.0};
            double size{0.0};
            std::uint64_t seq{0};
        };
        struct HeapCompare {
            bool is_buy{true};
            bool operator()(const HeapNode& a, const HeapNode& b) const {
                if (a.effective_price != b.effective_price) {
                    return is_buy
                        ? a.effective_price > b.effective_price
                        : a.effective_price < b.effective_price;
                }
                if (a.price != b.price) {
                    return is_buy ? a.price > b.price : a.price < b.price;
                }
                if (a.size != b.size) return a.size < b.size;
                return a.seq < b.seq;
            }
        };

        std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap(
            HeapCompare{is_buy}
        );

        auto make_node = [&](std::size_t cursor_idx) {
            const auto& c = cursors[cursor_idx];
            const double px = c.price();
            return HeapNode{
                cursor_idx,
                px,
                fee_adjusted_price(px, is_buy, c.taker_fee),
                c.size(),
                c.seq
            };
        };

        for (std::size_t i = 0; i < cursors.size(); ++i) {
            heap.push(make_node(i));
        }

        double remaining = quantity;
        while (remaining > kRoutingEps && !heap.empty()) {
            const auto lvl = heap.top();
            heap.pop();

            if (limit_price.has_value()) {
                if (is_buy && lvl.price > *limit_price) continue;
                if (!is_buy && lvl.price < *limit_price) continue;
            }

            const double take_qty = std::min(remaining, lvl.size);
            if (take_qty <= kRoutingEps) continue;

            const std::size_t state_idx = state_idx_by_cursor[lvl.cursor_idx];
            result.venue_qty[state_idx] += take_qty;
            result.venue_notional[state_idx] += (take_qty * lvl.price);

            result.raw_notional += (take_qty * lvl.price);
            result.filled_qty += take_qty;
            remaining -= take_qty;

            auto& cursor = cursors[lvl.cursor_idx];
            cursor.next();
            if (cursor.valid()) {
                heap.push(make_node(lvl.cursor_idx));
            }
        }

        return result;
    }

    static std::optional<std::size_t> choose_best_maker_venue(
        const std::vector<VenueState>& states,
        bool is_buy,
        double limit_price)
    {
        if (states.empty()) return std::nullopt;

        std::optional<std::size_t> best_idx;
        double best_effective = is_buy
            ? std::numeric_limits<double>::infinity()
            : -std::numeric_limits<double>::infinity();
        std::uint64_t best_seq = 0;

        for (std::size_t i = 0; i < states.size(); ++i) {
            if (!states[i].venue) continue;
            const double effective = maker_effective_limit_price(limit_price, is_buy, states[i].maker_fee);
            if (!best_idx.has_value()) {
                best_idx = i;
                best_effective = effective;
                best_seq = states[i].seq;
                continue;
            }

            bool take = false;
            if (is_buy) {
                if (effective < best_effective - kRoutingEps) {
                    take = true;
                } else if (std::abs(effective - best_effective) <= kRoutingEps) {
                    if (states[i].seq > best_seq) take = true;
                }
            } else {
                if (effective > best_effective + kRoutingEps) {
                    take = true;
                } else if (std::abs(effective - best_effective) <= kRoutingEps) {
                    if (states[i].seq > best_seq) take = true;
                }
            }

            if (take) {
                best_idx = i;
                best_effective = effective;
                best_seq = states[i].seq;
            }
        }

        return best_idx;
    }

public:
    static RoutingDecision route_order(
        const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
        const std::string& side_lower,
        double quantity,
        const std::optional<double>& limit_price,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info)
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

        const auto states = collect_venue_states(feeds, venue_static_info, venue_runtime_info);
        if (states.empty()) {
            out.message = "no liquidity available";
            return out;
        }

        const auto immediate = route_immediate_greedy(states, is_buy, quantity, limit_price);

        std::vector<double> venue_qty = immediate.venue_qty;
        std::vector<double> venue_notional = immediate.venue_notional;
        double total_notional = immediate.raw_notional;
        double routed_qty = immediate.filled_qty;
        double remaining = std::max(0.0, quantity - routed_qty);

        if (limit_price.has_value() && remaining > kRoutingEps) {
            const auto maker_idx = choose_best_maker_venue(states, is_buy, *limit_price);
            if (maker_idx.has_value()) {
                venue_qty[*maker_idx] += remaining;
                venue_notional[*maker_idx] += remaining * (*limit_price);
                total_notional += remaining * (*limit_price);
                routed_qty += remaining;
                remaining = 0.0;
            }
        }

        out.routable_qty = routed_qty;
        out.fully_routable = remaining <= kRoutingEps;
        if (out.routable_qty > kRoutingEps) {
            out.indicative_average_price = total_notional / out.routable_qty;
        }

        out.slices.reserve(states.size());
        for (std::size_t i = 0; i < states.size(); ++i) {
            const double q = venue_qty[i];
            if (q <= kRoutingEps) continue;
            out.slices.push_back(
                RouteSlice{
                    *states[i].venue,
                    ExecutionType::LIMIT_ALLOW_TAKER,
                    q,
                    venue_notional[i] / q
                }
            );
        }

        set_routing_message(out, limit_price);
        return out;
    }
};
