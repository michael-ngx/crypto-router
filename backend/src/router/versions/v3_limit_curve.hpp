#pragma once

#include "router/router_common.hpp"
#include "venues/venue_api.hpp"

namespace router_v3_detail {

// Heuristic coefficients.
// TODO: Tune from venue-specific execution data.
constexpr double kLatencyPenaltyBpsPerMs = 0.000001;
constexpr double kVolPenaltyMultiplier   = 0.50;
constexpr double kMinFillProb            = 0.02;
constexpr double kMaxFillProb            = 0.98;
constexpr double kUnderfillPenaltyFrac   = 0.0005;  // 5 bps of reference price per unfilled unit

struct VenueRouteState {
    std::string venue;
    double maker_fee{0.0};
    double taker_fee{0.0};
    double latency_ms{0.0};
    double volatility{0.0};
    std::shared_ptr<const BookSnapshot> snapshot;
};

struct CurveSegment {
    double capacity{0.0};                 // max child-order qty this segment can absorb
    double unit_objective_cost{0.0};      // signed objective cost per unit routed here
    double planned_price_per_unit{0.0};   // raw execution / posted price for output averaging
    double expected_fill_ratio{0.0};      // expected filled qty per 1 unit planned in this segment
};

struct ModeCurve {
    bool feasible{false};
    ExecutionType execution_type{LIMIT_POST_ONLY};
    std::vector<CurveSegment> segments;
};

struct VenueCurves {
    ModeCurve post_only;
    ModeCurve allow_taker;
};

struct HeapNode {
    int venue_index{-1};
    int mode_index{-1};      // 0 = POST_ONLY, 1 = ALLOW_TAKER
    int segment_index{-1};   // segment inside the chosen mode curve
    double unit_objective_cost{0.0};
    std::uint32_t generation{0}; // invalidates stale nodes
};

struct HeapCompare {
    bool operator()(const HeapNode& a, const HeapNode& b) const {
        if (a.unit_objective_cost != b.unit_objective_cost) {
            return a.unit_objective_cost > b.unit_objective_cost; // min-heap
        }
        if (a.venue_index != b.venue_index) return a.venue_index > b.venue_index;
        if (a.mode_index != b.mode_index) return a.mode_index > b.mode_index;
        return a.segment_index > b.segment_index;
    }
};

inline double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}

inline double effective_price(bool buy, double price, double fee)
{
    return buy ? price * (1.0 + fee) : price * (1.0 - fee);
}

inline double buy_taker_unit_cost(double raw_price, double taker_fee) {
    return raw_price * (1.0 + taker_fee);
}

inline double buy_maker_unit_cost(double raw_price, double maker_fee) {
    return raw_price * (1.0 + maker_fee);
}

inline double sell_taker_unit_cost(double raw_price, double taker_fee) {
    return -raw_price * (1.0 - taker_fee);
}

inline double sell_maker_unit_cost(double raw_price, double maker_fee) {
    return -raw_price * (1.0 - maker_fee);
}

inline bool post_only_would_cross(
    bool buy_side,
    double limit_price,
    const BookSnapshot& book)
{
    if (buy_side) {
        if (book.asks.empty()) return false;
        return limit_price >= book.asks.front().price - kRoutingEps;
    }

    if (book.bids.empty()) return false;
    return limit_price <= book.bids.front().price + kRoutingEps;
}

inline double queue_ahead_same_side(
    bool buy_side,
    double limit_price,
    const BookSnapshot& book)
{
    double q = 0.0;

    if (buy_side) {
        for (const auto& lvl : book.bids) {
            if (lvl.price > limit_price + kRoutingEps) {
                q += lvl.size;
            } else if (std::fabs(lvl.price - limit_price) <= kRoutingEps) {
                q += lvl.size;
            } else {
                break;
            }
        }
    } else {
        for (const auto& lvl : book.asks) {
            if (lvl.price < limit_price - kRoutingEps) {
                q += lvl.size;
            } else if (std::fabs(lvl.price - limit_price) <= kRoutingEps) {
                q += lvl.size;
            } else {
                break;
            }
        }
    }

    return q;
}

inline double top_opposite_size(
    bool buy_side,
    const BookSnapshot& book)
{
    if (buy_side) {
        return book.asks.empty() ? 0.0 : book.asks.front().size;
    }
    return book.bids.empty() ? 0.0 : book.bids.front().size;
}

inline bool improves_spread(
    bool buy_side,
    double limit_price,
    const BookSnapshot& book)
{
    if (buy_side) {
        if (book.bids.empty()) return false;
        if (book.asks.empty()) return true;
        return (limit_price > book.bids.front().price + kRoutingEps) &&
               (limit_price < book.asks.front().price - kRoutingEps);
    }

    if (book.asks.empty()) return false;
    if (book.bids.empty()) return true;
    return (limit_price < book.asks.front().price - kRoutingEps) &&
           (limit_price > book.bids.front().price + kRoutingEps);
}

inline bool joins_touch_without_improving(
    bool buy_side,
    double limit_price,
    const BookSnapshot& book)
{
    if (buy_side) {
        if (book.bids.empty()) return false;
        return std::fabs(limit_price - book.bids.front().price) <= kRoutingEps;
    }

    if (book.asks.empty()) return false;
    return std::fabs(limit_price - book.asks.front().price) <= kRoutingEps;
}

/*
This probability is intentionally state-based, not qty-based.
That keeps the posted segment linear in routed quantity.
*/
inline double heuristic_fill_probability_at_limit(
    bool buy_side,
    double limit_price,
    const BookSnapshot& book,
    double latency_ms,
    double volatility)
{
    double base = 0.15;
    if (improves_spread(buy_side, limit_price, book)) {
        base = 0.70;
    } else if (joins_touch_without_improving(buy_side, limit_price, book)) {
        base = 0.45;
    }

    const double queue_ahead = queue_ahead_same_side(buy_side, limit_price, book);
    const double top_opp = std::max(top_opposite_size(buy_side, book), 1e-9);

    const double queue_factor = std::exp(-queue_ahead / top_opp);
    const double lat_factor   = std::exp(-0.0020 * std::max(latency_ms, 0.0));
    const double vol_factor   = std::exp(-5.0 * std::max(volatility, 0.0));

    const double p = base * std::sqrt(queue_factor) * lat_factor * vol_factor;
    return clamp(p, kMinFillProb, kMaxFillProb);
}

inline double urgency_penalty_per_unit(
    double reference_price,
    double latency_ms,
    double volatility)
{
    const double lat_pen = reference_price * kLatencyPenaltyBpsPerMs * std::max(latency_ms, 0.0);
    const double vol_pen = reference_price * kVolPenaltyMultiplier * std::max(volatility, 0.0);
    return lat_pen + vol_pen;
}

inline double underfill_penalty_per_unit(double reference_price) {
    return reference_price * kUnderfillPenaltyFrac;
}

inline double best_reference_taker_unit_cost(
    bool buy_side,
    const std::vector<VenueRouteState>& venues)
{
    double best = std::numeric_limits<double>::infinity();

    for (const auto& vr : venues) {
        if (!vr.snapshot) continue;
        const auto& book = *vr.snapshot;

        if (buy_side) {
            if (book.asks.empty()) continue;
            const double c = buy_taker_unit_cost(book.asks.front().price, vr.taker_fee);
            best = std::min(best, c);
        } else {
            if (book.bids.empty()) continue;
            const double c = sell_taker_unit_cost(book.bids.front().price, vr.taker_fee);
            best = std::min(best, c);
        }
    }

    if (!std::isfinite(best)) return 0.0;
    return best;
}

inline double global_mid_price_estimate(const std::vector<VenueRouteState>& venues)
{
    double best_bid = -std::numeric_limits<double>::infinity();
    double best_ask =  std::numeric_limits<double>::infinity();

    for (const auto& vr : venues) {
        if (!vr.snapshot) continue;
        const auto& book = *vr.snapshot;

        if (!book.bids.empty()) best_bid = std::max(best_bid, book.bids.front().price);
        if (!book.asks.empty()) best_ask = std::min(best_ask, book.asks.front().price);
    }

    if (std::isfinite(best_bid) && std::isfinite(best_ask) && best_bid <= best_ask) {
        return 0.5 * (best_bid + best_ask);
    }
    if (std::isfinite(best_bid)) return best_bid;
    if (std::isfinite(best_ask)) return best_ask;
    return 0.0;
}

inline double posted_unit_objective_cost(
    bool buy_side,
    double limit_price,
    double maker_fee,
    double p_fill,
    double fallback_reference_unit_cost,
    double reference_price,
    double latency_ms,
    double volatility)
{
    const double maker_unit_cost = buy_side
        ? buy_maker_unit_cost(limit_price, maker_fee)
        : sell_maker_unit_cost(limit_price, maker_fee);

    const double urgency_pen = urgency_penalty_per_unit(
        reference_price,
        latency_ms,
        volatility);

    const double underfill_pen = underfill_penalty_per_unit(reference_price);

    return
        p_fill * maker_unit_cost +
        (1.0 - p_fill) * (fallback_reference_unit_cost + urgency_pen + underfill_pen);
}

inline ModeCurve build_post_only_curve(
    bool buy_side,
    double total_qty_cap,
    double limit_price,
    const VenueRouteState& vr,
    double fallback_reference_unit_cost,
    double reference_price)
{
    ModeCurve curve;
    curve.execution_type = LIMIT_POST_ONLY;

    if (!vr.snapshot || total_qty_cap <= kRoutingEps) {
        return curve;
    }

    const auto& book = *vr.snapshot;
    if (post_only_would_cross(buy_side, limit_price, book)) {
        return curve; // infeasible
    }

    const double p_fill = heuristic_fill_probability_at_limit(
        buy_side,
        limit_price,
        book,
        vr.latency_ms,
        vr.volatility);

    const double unit_obj = posted_unit_objective_cost(
        buy_side,
        limit_price,
        vr.maker_fee,
        p_fill,
        fallback_reference_unit_cost,
        reference_price,
        vr.latency_ms,
        vr.volatility);

    curve.feasible = true;
    curve.segments.push_back(CurveSegment{
        total_qty_cap,
        unit_obj,
        limit_price,
        p_fill
    });

    return curve;
}

inline ModeCurve build_allow_taker_curve(
    bool buy_side,
    double total_qty_cap,
    double limit_price,
    const VenueRouteState& vr,
    double fallback_reference_unit_cost,
    double reference_price)
{
    ModeCurve curve;
    curve.execution_type = LIMIT_ALLOW_TAKER;

    if (!vr.snapshot || total_qty_cap <= kRoutingEps) {
        return curve;
    }

    const auto& book = *vr.snapshot;
    const auto& opp_levels = buy_side ? book.asks : book.bids;

    double remaining = total_qty_cap;

    // Deterministic taker prefix from the crossing part of one allow-taker order.
    for (const auto& lvl : opp_levels) {
        if (remaining <= kRoutingEps) break;

        const bool crosses = buy_side
            ? (lvl.price <= limit_price + kRoutingEps)
            : (lvl.price >= limit_price - kRoutingEps);

        if (!crosses) break;
        if (lvl.size <= kRoutingEps) continue;

        const double cap = std::min(remaining, lvl.size);
        const double unit_obj = buy_side
            ? buy_taker_unit_cost(lvl.price, vr.taker_fee)
            : sell_taker_unit_cost(lvl.price, vr.taker_fee);

        curve.segments.push_back(CurveSegment{
            cap,
            unit_obj,
            lvl.price,
            1.0
        });

        remaining -= cap;
    }

    // Residual resting portion.
    if (remaining > kRoutingEps) {
        const double p_fill = heuristic_fill_probability_at_limit(
            buy_side,
            limit_price,
            book,
            vr.latency_ms,
            vr.volatility);

        const double unit_obj = posted_unit_objective_cost(
            buy_side,
            limit_price,
            vr.maker_fee,
            p_fill,
            fallback_reference_unit_cost,
            reference_price,
            vr.latency_ms,
            vr.volatility);

        curve.segments.push_back(CurveSegment{
            remaining,
            unit_obj,
            limit_price,
            p_fill
        });
    }

    curve.feasible = !curve.segments.empty();
    return curve;
}

inline const ModeCurve& curve_for_mode(
    const VenueCurves& curves,
    int mode_index)
{
    return (mode_index == 0) ? curves.post_only : curves.allow_taker;
}

} // namespace router_v3_detail

struct RouterV3LimitCurve {
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

        const bool buy_side = side_lower == "buy";

        if (side_lower != "buy" && side_lower != "sell") {
            out.message = "Invalid side_lower. Expected 'buy' or 'sell'.";
            return out;
        }

        if (quantity <= kRoutingEps) {
            out.fully_routable = true;
            out.routable_qty = 0.0;
            out.indicative_average_price = 0.0;
            out.message = "Zero quantity requested.";
            return out;
        }

        if (!limit_price) {
            return route_market_order_heap(
                feeds,
                buy_side,
                quantity,
                venue_static_info,
                venue_runtime_info,
                out);
        }

        return route_limit_order_expected_cost(
            feeds,
            buy_side,
            quantity,
            *limit_price,
            venue_static_info,
            venue_runtime_info,
            out);
    }

private:
    static double taker_fee_for_venue(
        const std::string& venue,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info)
    {
        auto it = venue_static_info.find(venue);
        if (it == venue_static_info.end()) return 0.0;

        double vol = 0.0;
        auto runtime_it = venue_runtime_info.find(venue);
        if (runtime_it != venue_runtime_info.end()) {
            vol = std::max(0.0, runtime_it->second.trailing_volume_usd);
        }

        return it->second.fees.tier_for_volume(vol).taker_fee;
    }

    static double maker_fee_for_venue(
        const std::string& venue,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info)
    {
        auto it = venue_static_info.find(venue);
        if (it == venue_static_info.end()) return 0.0;

        double vol = 0.0;
        auto runtime_it = venue_runtime_info.find(venue);
        if (runtime_it != venue_runtime_info.end()) {
            vol = std::max(0.0, runtime_it->second.trailing_volume_usd);
        }

        return it->second.fees.tier_for_volume(vol).maker_fee;
    }

    /*
    * ****************************************************************
    * Market order routing via heap-based SOR
    * ****************************************************************
    */
    static RoutingDecision route_market_order_heap(
        const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
        bool buy,
        double quantity,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info,
        RoutingDecision& out)
    {
        struct VenueBook {
            std::string venue;
            double taker_fee;
            std::shared_ptr<const BookSnapshot> snapshot;
        };

        struct HeapNode {
            int venue_index;
            int level_index;
            double effective_price;
        };

        struct HeapCompare {
            bool buy{true};

            bool operator()(const HeapNode& a, const HeapNode& b) const {
                if (a.effective_price != b.effective_price) {
                    // Buy: lower effective price is better.
                    // Sell: higher effective price is better.
                    return buy
                        ? a.effective_price > b.effective_price
                        : a.effective_price < b.effective_price;
                }

                // Deterministic tie-breakers.
                if (a.venue_index != b.venue_index) return a.venue_index > b.venue_index;
                return a.level_index > b.level_index;
            }
        };
        
        /*
        * (1) Load snapshots and per-venue taker fee. Store snapshot and fee together in VenueBook struct
        */
        std::vector<VenueBook> books;
        books.reserve(feeds.size());

        for (const auto& feed : feeds) {
            if (!feed) continue;
            auto snap = feed->load_snapshot();
            if (!snap) continue;

            const double fee = taker_fee_for_venue(
                snap->venue,
                venue_static_info,
                venue_runtime_info);

            books.push_back({snap->venue, fee, snap});
        }

        /*
        * (2) Initialize a heap with best level per venue, using **fee-adjusted** effective price
        */
        const int venue_count = static_cast<int>(books.size());
        std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap(
            HeapCompare{buy}
        );

        for (int i = 0; i < venue_count; ++i) {
            const auto& book = *books[i].snapshot;
            const auto& levels = buy ? book.asks : book.bids;
            if (levels.empty()) continue;

            const double eff = router_v3_detail::effective_price(
                buy,
                levels[0].price,
                books[i].taker_fee);

            heap.push({i, 0, eff});
        }
        
        /*
        * (3) Pops best level repeatedly, consumes quantity, pushes next level from same venue until order fully filled/heap exhausted.
        */
        std::vector<double> qty_by_venue(venue_count, 0.0);
        std::vector<double> notional_by_venue(venue_count, 0.0);

        double remaining = quantity;

        while (!heap.empty() && remaining > kRoutingEps) {
            const HeapNode node = heap.top();
            heap.pop();

            const int venue_idx = node.venue_index;
            const auto& vb = books[venue_idx];      //TODO inefficient copy?????
            const auto& book = *vb.snapshot;

            const auto& levels = buy ? book.asks : book.bids;
            const auto& lvl = levels[node.level_index];

            const double take = std::min(lvl.size, remaining);
            if (take <= kRoutingEps) continue;

            qty_by_venue[venue_idx] += take;
            notional_by_venue[venue_idx] += take * lvl.price;
            remaining -= take;

            const int next = node.level_index + 1;
            if (next < static_cast<int>(levels.size())) {
                const double eff = router_v3_detail::effective_price(
                    buy,
                    levels[next].price,
                    vb.taker_fee);
                heap.push({venue_idx, next, eff});
            }
        }
        

        /*
        * (4) Aggregate per-venue execution slices (RouteSlice)
        */
        double total_notional = 0.0;
        double total_qty = 0.0;

        out.slices.reserve(venue_count);

        for (int i = 0; i < venue_count; ++i) {
            const double q = qty_by_venue[i];
            if (q <= kRoutingEps) continue;

            const double notional = notional_by_venue[i];

            RouteSlice slice;
            slice.venue = books[i].venue;
            slice.execution_type = MARKET;
            slice.quantity = q;
            slice.price = notional / q;

            out.slices.push_back(slice);

            total_notional += notional;
            total_qty += q;
        }

        out.routable_qty = total_qty;
        out.fully_routable = remaining <= kRoutingEps;
        out.indicative_average_price = (total_qty > kRoutingEps)
            ? (total_notional / total_qty)
            : 0.0;

        out.message = out.fully_routable
            ? "Market order routed via heap-based SOR."
            : "Market order partially filled due to insufficient liquidity.";

        return out;
    }


    /*
    * ****************************************************************
    * Limit order routing via frontier-based SOR with expected cost optimization
    * ****************************************************************
    */
    static RoutingDecision route_limit_order_expected_cost(
        const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
        bool buy_side,
        double quantity,
        double limit_price,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info,
        RoutingDecision& out)
    {
        using namespace router_v3_detail;

        std::vector<VenueRouteState> venues;
        venues.reserve(feeds.size());

        for (const auto& feed : feeds) {
            if (!feed) continue;
            auto snap = feed->load_snapshot();
            if (!snap) continue;

            VenueRouteState vr;
            vr.venue = snap->venue;
            vr.snapshot = snap;
            vr.maker_fee = maker_fee_for_venue(
                snap->venue,
                venue_static_info,
                venue_runtime_info);
            vr.taker_fee = taker_fee_for_venue(
                snap->venue,
                venue_static_info,
                venue_runtime_info);

            if (auto runtime_it = venue_runtime_info.find(snap->venue);
                runtime_it != venue_runtime_info.end()) {
                vr.latency_ms = runtime_it->second.latency_ms;
                vr.volatility = runtime_it->second.volatility;
            }

            venues.push_back(std::move(vr));
        }

        if (venues.empty()) {
            out.message = "No venue snapshots available.";
            return out;
        }

        const int venue_count = static_cast<int>(venues.size());

        const double fallback_reference_unit_cost =
            best_reference_taker_unit_cost(buy_side, venues);

        double reference_price = global_mid_price_estimate(venues);
        if (reference_price <= 0.0) {
            reference_price = limit_price;
        }

        std::vector<VenueCurves> all_curves(venue_count);
        for (int v = 0; v < venue_count; ++v) {
            all_curves[v].post_only = build_post_only_curve(
                buy_side,
                quantity,
                limit_price,
                venues[v],
                fallback_reference_unit_cost,
                reference_price);

            all_curves[v].allow_taker = build_allow_taker_curve(
                buy_side,
                quantity,
                limit_price,
                venues[v],
                fallback_reference_unit_cost,
                reference_price);
        }

        std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap;

        // Per-venue state.
        // chosen_mode_by_venue: -1 unset, 0 POST_ONLY, 1 ALLOW_TAKER
        std::vector<int> chosen_mode_by_venue(venue_count, -1);
        std::vector<std::uint32_t> generation_by_venue(venue_count, 0);

        // Aggregation arrays.
        std::vector<double> qty_by_venue(venue_count, 0.0);
        std::vector<double> planned_notional_by_venue(venue_count, 0.0);

        // Seed heap with both initial mode frontiers where feasible.
        for (int v = 0; v < venue_count; ++v) {
            if (all_curves[v].post_only.feasible && !all_curves[v].post_only.segments.empty()) {
                heap.push(HeapNode{
                    v,
                    0,
                    0,
                    all_curves[v].post_only.segments[0].unit_objective_cost,
                    generation_by_venue[v]
                });
            }
            if (all_curves[v].allow_taker.feasible && !all_curves[v].allow_taker.segments.empty()) {
                heap.push(HeapNode{
                    v,
                    1,
                    0,
                    all_curves[v].allow_taker.segments[0].unit_objective_cost,
                    generation_by_venue[v]
                });
            }
        }

        double remaining = quantity;
        double total_planned_notional = 0.0;
        double total_expected_fill = 0.0;

        while (!heap.empty() && remaining > kRoutingEps) {
            const HeapNode node = heap.top();
            heap.pop();

            const int v = node.venue_index;

            // Drop stale nodes.
            if (node.generation != generation_by_venue[v]) {
                continue;
            }

            // Venue-mode locking:
            // - if venue unset, first accepted node locks the mode
            // - if venue already locked to another mode, ignore
            if (chosen_mode_by_venue[v] == -1) {
                chosen_mode_by_venue[v] = node.mode_index;
                ++generation_by_venue[v];
            } else if (chosen_mode_by_venue[v] != node.mode_index) {
                continue;
            } else if (node.generation != generation_by_venue[v] - 1) {
                // This node belonged to the pre-lock generation and should be ignored.
                continue;
            }

            const auto& curve = curve_for_mode(all_curves[v], chosen_mode_by_venue[v]);
            if (node.segment_index < 0 ||
                node.segment_index >= static_cast<int>(curve.segments.size())) {
                continue;
            }

            const auto& seg = curve.segments[node.segment_index];
            if (seg.capacity <= kRoutingEps) {
                continue;
            }

            const double take = std::min(seg.capacity, remaining);
            if (take <= kRoutingEps) {
                continue;
            }

            qty_by_venue[v] += take;
            planned_notional_by_venue[v] += take * seg.planned_price_per_unit;

            total_planned_notional += take * seg.planned_price_per_unit;
            total_expected_fill += take * seg.expected_fill_ratio;
            remaining -= take;

            // Only continue along the same mode frontier if this segment was fully consumed.
            if (take >= seg.capacity - kRoutingEps) {
                const int next_seg = node.segment_index + 1;
                if (next_seg < static_cast<int>(curve.segments.size())) {
                    heap.push(HeapNode{
                        v,
                        chosen_mode_by_venue[v],
                        next_seg,
                        curve.segments[next_seg].unit_objective_cost,
                        generation_by_venue[v]
                    });
                }
            }
        }

        out.slices.reserve(venue_count);

        for (int v = 0; v < venue_count; ++v) {
            if (qty_by_venue[v] <= kRoutingEps) continue;

            RouteSlice slice;
            slice.venue = venues[v].venue;
            slice.execution_type = (chosen_mode_by_venue[v] == 1)
                ? LIMIT_ALLOW_TAKER
                : LIMIT_POST_ONLY;
            slice.quantity = qty_by_venue[v];
            slice.price = planned_notional_by_venue[v] / qty_by_venue[v];

            out.slices.push_back(slice);
        }

        std::sort(
            out.slices.begin(),
            out.slices.end(),
            [](const RouteSlice& a, const RouteSlice& b) {
                if (a.execution_type != b.execution_type) {
                    return static_cast<int>(a.execution_type) < static_cast<int>(b.execution_type);
                }
                return a.venue < b.venue;
            });

        out.routable_qty = quantity - remaining;
        out.indicative_average_price = (out.routable_qty > kRoutingEps)
            ? (total_planned_notional / out.routable_qty)
            : 0.0;

        out.fully_routable = remaining <= kRoutingEps;
        const bool expected_full_fill = total_expected_fill >= quantity - kRoutingEps;

        if (out.fully_routable) {
            out.message = expected_full_fill
                ? "Limit order routed via frontier optimizer."
                : "Limit order fully planned; expected fill is partial under current model.";
        } else if (out.routable_qty > kRoutingEps) {
            out.message = "Limit order only partially routable under current venue/mode frontier.";
        } else {
            out.message = "Limit order not routable under current venue/mode frontier.";
        }

        return out;
    }
};
