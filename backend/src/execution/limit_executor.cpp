#include "limit_executor.hpp"
#include "execution/fill_simulator.hpp"
#include <pqxx/pqxx>
#include <thread>
#include <chrono>
#include <iostream>
#include <algorithm>
#include "supabase/storage_supabase.hpp"

namespace {

struct LegState {
    std::string   venue;
    std::string   sim_id;
    ExecutionType exec_type;
    double        planned_qty{0.0};
    double        limit_price{0.0};   // user's limit price (fill constraint)
    double        planned_price{0.0}; // indicative from routing (for DB display)

    double filled_qty{0.0};
    double total_notional{0.0};
    double commission{0.0};

    bool first_fill_recorded{false};
    bool submitted{false};
    bool rejected{false};
    bool done{false};
};

double safe_avg(double notional, double qty) {
    return qty > 1e-12 ? notional / qty : 0.0;
}

// ── DB helpers ──────────────────────────────────────────────────────────────

void db_set_executing(const std::string& db_conn, const std::string& order_id) {
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);
        txn.exec("UPDATE public.orders SET status='executing', "
                 "execution_started_at=NOW(), last_updated_at=NOW() "
                 "WHERE id=$1",
                 pqxx::params(order_id));
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_set_executing: " << e.what() << "\n";
    }
}

// Record submission + acknowledgement for a leg (steps 3+4 in the UI timeline).
void db_submit_leg(const std::string& db_conn,
                   const std::string& order_id,
                   const LegState& leg)
{
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);
        txn.exec(
            "UPDATE public.order_legs SET "
            "submitted_at=NOW(), acknowledged_at=NOW(), "
            "client_order_id=$1, venue_order_id=$1, "
            "quantity_submitted=$2, price_submitted=$3, "
            "last_updated_at=NOW() "
            "WHERE order_id=$4 AND venue=$5",
            pqxx::params(leg.sim_id, leg.planned_qty, leg.limit_price,
                         order_id, leg.venue));
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_submit_leg: " << e.what() << "\n";
    }
}

void db_reject_leg(const std::string& db_conn,
                   const std::string& order_id,
                   const LegState& leg)
{
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);
        txn.exec(
            "UPDATE public.order_legs SET "
            "status='failed', "
            "error_code='POST_ONLY_REJECTED', "
            "error_message='post-only order rejected: would cross spread on arrival', "
            "terminal_at=NOW(), last_updated_at=NOW() "
            "WHERE order_id=$1 AND venue=$2",
            pqxx::params(order_id, leg.venue));
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_reject_leg: " << e.what() << "\n";
    }
}

// Update a leg after a fill event. set_terminal=true marks it as fully filled.
void db_update_leg_fill(const std::string& db_conn,
                        const std::string& order_id,
                        LegState& leg,
                        bool set_terminal)
{
    const double avg_px = safe_avg(leg.total_notional, leg.filled_qty);
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);
        if (!leg.first_fill_recorded) {
            // Only set status to filled if terminal; otherwise leave as planned
            // so we don't write an invalid enum value for partial mid-fill state.
            if (set_terminal) {
                txn.exec(
                    "UPDATE public.order_legs SET "
                    "status='filled', quantity_filled=$1, price_filled_avg=$2, "
                    "commission_usd=$3, first_fill_at=NOW(), last_fill_at=NOW(), "
                    "terminal_at=NOW(), last_updated_at=NOW() "
                    "WHERE order_id=$4 AND venue=$5",
                    pqxx::params(leg.filled_qty, avg_px, leg.commission,
                                 order_id, leg.venue));
            } else {
                txn.exec(
                    "UPDATE public.order_legs SET "
                    "quantity_filled=$1, price_filled_avg=$2, "
                    "commission_usd=$3, first_fill_at=NOW(), last_fill_at=NOW(), "
                    "last_updated_at=NOW() "
                    "WHERE order_id=$4 AND venue=$5",
                    pqxx::params(leg.filled_qty, avg_px, leg.commission,
                                 order_id, leg.venue));
            }
            leg.first_fill_recorded = true;
        } else {
            if (set_terminal) {
                txn.exec(
                    "UPDATE public.order_legs SET "
                    "status='filled', quantity_filled=$1, price_filled_avg=$2, "
                    "commission_usd=$3, last_fill_at=NOW(), terminal_at=NOW(), "
                    "last_updated_at=NOW() "
                    "WHERE order_id=$4 AND venue=$5",
                    pqxx::params(leg.filled_qty, avg_px, leg.commission,
                                 order_id, leg.venue));
            } else {
                txn.exec(
                    "UPDATE public.order_legs SET "
                    "quantity_filled=$1, price_filled_avg=$2, "
                    "commission_usd=$3, last_fill_at=NOW(), last_updated_at=NOW() "
                    "WHERE order_id=$4 AND venue=$5",
                    pqxx::params(leg.filled_qty, avg_px, leg.commission,
                                 order_id, leg.venue));
            }
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_update_leg_fill: " << e.what() << "\n";
    }
}

// Sync order-level filled quantities from all legs.
void db_sync_order_quantities(const std::string& db_conn,
                              const std::string& order_id,
                              const std::vector<LegState>& legs)
{
    double total_qty = 0.0, total_notional = 0.0;
    for (const auto& l : legs) {
        total_qty      += l.filled_qty;
        total_notional += l.total_notional;
    }
    const double avg_px = safe_avg(total_notional, total_qty);
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);
        txn.exec(
            "UPDATE public.orders SET "
            "quantity_filled=$1, price_filled_avg=$2, last_updated_at=NOW() "
            "WHERE id=$3",
            pqxx::params(total_qty, avg_px, order_id));
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_sync_order_quantities: " << e.what() << "\n";
    }
}

void db_finalize_order(const std::string& db_conn,
                       const std::string& order_id,
                       const std::vector<LegState>& legs,
                       double requested_qty)
{
    double total_qty = 0.0, total_notional = 0.0, total_commission = 0.0;
    for (const auto& l : legs) {
        total_qty        += l.filled_qty;
        total_notional   += l.total_notional;
        total_commission += l.commission;
    }

    // Any leg not yet at a terminal status in DB needs its terminal_at set.
    // We mark unfilled/partial legs as expired.
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn));
        pqxx::work txn(conn);

        for (const auto& leg : legs) {
            if (!leg.done && !leg.rejected) {
                const std::string leg_status =
                    leg.filled_qty > 1e-12 ? "expired" : "expired"; // leg always expires; fill qty already recorded
                txn.exec(
                    "UPDATE public.order_legs SET "
                    "status=$1, terminal_at=NOW(), last_updated_at=NOW() "
                    "WHERE order_id=$2 AND venue=$3",
                    pqxx::params(leg_status, order_id, leg.venue));
            }
        }

        std::string order_status;
        if (total_qty <= 1e-12) {
            // Determine if it was a rejection or expiry
            bool all_rejected = std::all_of(legs.begin(), legs.end(),
                [](const LegState& l){ return l.rejected; });
            order_status = all_rejected ? "failed" : "expired";
        } else if (total_qty >= requested_qty - 1e-12) {
            order_status = "filled";
        } else {
            // Partially filled and now terminal (expired with partial fill)
            order_status = "partially_filled";
        }

        const double avg_px = safe_avg(total_notional, total_qty);
        txn.exec(
            "UPDATE public.orders SET "
            "status=$1, quantity_filled=$2, price_filled_avg=$3, "
            "total_commission_usd=$4, terminal_at=NOW(), last_updated_at=NOW() "
            "WHERE id=$5",
            pqxx::params(order_status, total_qty, avg_px,
                         total_commission, order_id));
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[limit_executor] db_finalize_order: " << e.what() << "\n";
    }
}

} // namespace

// ── LimitExecutor ────────────────────────────────────────────────────────────

double LimitExecutor::resolve_fee(
    const std::string& venue,
    bool taker,
    const std::unordered_map<std::string, VenueRuntimeInfo>& runtime_info) const
{
    auto sit = venue_static_info_.find(venue);
    if (sit == venue_static_info_.end()) return 0.0;
    double trailing_vol = 0.0;
    auto rit = runtime_info.find(venue);
    if (rit != runtime_info.end())
        trailing_vol = rit->second.trailing_volume_usd;
    const auto tier = sit->second.fees.tier_for_volume(trailing_vol);
    return taker ? tier.taker_fee : tier.maker_fee;
}

void LimitExecutor::execute_async(
    std::string order_id,
    std::string symbol,
    std::string side,
    double limit_price,
    RoutingDecision routing,
    std::unordered_map<std::string, VenueRuntimeInfo> venue_runtime_info) const
{
    // Capture server-lifetime refs directly — do NOT capture `this`, which is
    // stack-allocated in the request handler and destroyed before the thread runs.
    FeedManager& feeds_ref                   = feeds_;
    const std::string db_copy                = db_conn_str_;
    const std::unordered_map<std::string, VenueStaticInfo>& static_ref = venue_static_info_;
    const LimitOrderConfig cfg               = config_;

    std::thread([&feeds_ref, db_copy, &static_ref, cfg,
                 order_id           = std::move(order_id),
                 symbol             = std::move(symbol),
                 side               = std::move(side),
                 limit_price,
                 routing            = std::move(routing),
                 venue_runtime_info = std::move(venue_runtime_info)]()
    {
        try {
            LimitExecutor exec(feeds_ref, db_copy, static_ref, cfg);
            exec.run(order_id, symbol, side, limit_price, routing, venue_runtime_info);
        } catch (const std::exception& e) {
            std::cerr << "[limit_executor] unhandled exception for order "
                      << order_id << ": " << e.what() << "\n";
        }
    }).detach();
}

void LimitExecutor::run(
    const std::string& order_id,
    const std::string& symbol,
    const std::string& side,
    double limit_price,
    const RoutingDecision& routing,
    const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info) const
{
    // Hold routing guard for full lifetime — prevents feed sweep.
    auto inputs = feeds_.acquire_routing_inputs(symbol);
    if (!inputs) {
        std::cerr << "[limit_executor] could not acquire feeds for " << symbol << "\n";
        return;
    }

    // Index feeds by venue.
    std::unordered_map<std::string, std::shared_ptr<IVenueFeed>> feeds_by_venue;
    for (const auto& f : inputs->feeds)
        feeds_by_venue[f->venue()] = f;

    // Build per-leg state.
    std::vector<LegState> legs;
    legs.reserve(routing.slices.size());
    for (const auto& slice : routing.slices) {
        LegState s;
        s.venue         = slice.venue;
        s.sim_id        = "LIM-" + order_id.substr(0, 8) + "-" + slice.venue;
        s.exec_type     = slice.execution_type;
        s.planned_qty   = slice.quantity;
        s.limit_price   = limit_price;
        s.planned_price = slice.price;
        legs.push_back(std::move(s));
    }

    db_set_executing(db_conn_str_, order_id);

    const auto deadline = std::chrono::steady_clock::now() + config_.ttl;

    // ── Arrival check ────────────────────────────────────────────────────────
    for (auto& leg : legs) {
        auto fit = feeds_by_venue.find(leg.venue);
        if (fit == feeds_by_venue.end()) {
            leg.rejected = leg.done = true;
            continue;
        }
        auto snap = fit->second->load_snapshot();
        if (!snap) {
            leg.rejected = leg.done = true;
            continue;
        }

        if (leg.exec_type == ExecutionType::LIMIT_POST_ONLY) {
            if (crosses_spread(*snap, side, leg.limit_price)) {
                leg.rejected = leg.done = true;
                db_reject_leg(db_conn_str_, order_id, leg);
                continue;
            }
        } else {
            // LIMIT_ALLOW_TAKER: fill crossing portion immediately at taker fee.
            if (crosses_spread(*snap, side, leg.limit_price)) {
                double taker_fee = resolve_fee(leg.venue, true, venue_runtime_info);
                auto fill = simulate_limit_fill(
                    *snap, leg.venue, side, leg.planned_qty, leg.limit_price, taker_fee);
                if (fill.quantity_filled > 1e-12) {
                    leg.filled_qty    += fill.quantity_filled;
                    leg.total_notional += fill.total_notional;
                    leg.commission    += fill.commission_usd;
                    if (leg.filled_qty >= leg.planned_qty - 1e-12)
                        leg.done = true;
                }
            }
        }

        // Mark non-rejected legs as submitted (simulates steps 3+4 in UI).
        db_submit_leg(db_conn_str_, order_id, leg);
        leg.submitted = true;

        if (leg.filled_qty > 1e-12) {
            db_update_leg_fill(db_conn_str_, order_id, leg, leg.done);
            db_sync_order_quantities(db_conn_str_, order_id, legs);
        }
    }

    const bool all_rejected = std::all_of(legs.begin(), legs.end(),
        [](const LegState& l){ return l.rejected; });
    if (all_rejected) {
        db_finalize_order(db_conn_str_, order_id, legs, routing.requested_qty);
        return;
    }

    const bool all_done = std::all_of(legs.begin(), legs.end(),
        [](const LegState& l){ return l.done || l.rejected; });
    if (all_done) {
        db_finalize_order(db_conn_str_, order_id, legs, routing.requested_qty);
        return;
    }

    // ── Poll loop for resting quantity ───────────────────────────────────────
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(config_.poll_interval);
        if (std::chrono::steady_clock::now() >= deadline) break;

        for (auto& leg : legs) {
            if (leg.done || leg.rejected) continue;

            auto fit = feeds_by_venue.find(leg.venue);
            if (fit == feeds_by_venue.end()) continue;
            auto snap = fit->second->load_snapshot();
            if (!snap) continue;

            const double remaining = leg.planned_qty - leg.filled_qty;
            if (remaining <= 1e-12) { leg.done = true; continue; }

            if (!crosses_spread(*snap, side, leg.limit_price)) continue;

            double maker_fee = resolve_fee(leg.venue, false, venue_runtime_info);
            // Resting maker: fills at exactly limit_price, not at ask prices.
            auto fill = simulate_resting_fill(
                *snap, leg.venue, side, remaining, leg.limit_price, maker_fee);
            if (fill.quantity_filled <= 1e-12) continue;

            leg.filled_qty     += fill.quantity_filled;
            leg.total_notional += fill.total_notional;
            leg.commission     += fill.commission_usd;
            if (leg.filled_qty >= leg.planned_qty - 1e-12)
                leg.done = true;

            db_update_leg_fill(db_conn_str_, order_id, leg, leg.done);
            db_sync_order_quantities(db_conn_str_, order_id, legs);
        }

        const bool finished = std::all_of(legs.begin(), legs.end(),
            [](const LegState& l){ return l.done || l.rejected; });
        if (finished) break;
    }

    db_finalize_order(db_conn_str_, order_id, legs, routing.requested_qty);
}
