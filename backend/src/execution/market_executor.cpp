#include "market_executor.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "supabase/storage_supabase.hpp"

namespace {

// Adds commission columns if they don't exist yet. Runs once per process.
void run_schema_migration(const std::string& db_conn_str) {
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn_str));
        pqxx::work txn(conn);
        txn.exec("ALTER TABLE public.orders     ADD COLUMN IF NOT EXISTS total_commission_usd DOUBLE PRECISION DEFAULT 0");
        txn.exec("ALTER TABLE public.order_legs ADD COLUMN IF NOT EXISTS commission_usd       DOUBLE PRECISION DEFAULT 0");
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "[market_executor] schema migration warning: " << e.what() << "\n";
    }
}

} // namespace

void MarketExecutor::ensure_schema() const {
    static std::once_flag flag;
    std::call_once(flag, [this]() { run_schema_migration(db_conn_str_); });
}

double MarketExecutor::resolve_taker_fee(
    const std::string& venue,
    const std::unordered_map<std::string, VenueRuntimeInfo>& runtime_info) const
{
    auto sit = venue_static_info_.find(venue);
    if (sit == venue_static_info_.end()) return 0.0;

    double trailing_vol = 0.0;
    auto rit = runtime_info.find(venue);
    if (rit != runtime_info.end())
        trailing_vol = rit->second.trailing_volume_usd;

    return sit->second.fees.tier_for_volume(trailing_vol).taker_fee;
}

MarketExecutionResult MarketExecutor::execute(
    const std::string& order_id,
    const std::string& symbol,
    const std::string& side,
    const RoutingDecision& routing,
    const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info) const
{
    ensure_schema();

    // Acquire live snapshots for all venues supporting this symbol.
    auto inputs = feeds_.acquire_routing_inputs(symbol);
    if (!inputs)
        return {OrderFillResult{}, false, "could not acquire routing inputs for " + symbol};

    // Index snapshots by venue name.
    std::unordered_map<std::string, std::shared_ptr<const BookSnapshot>> snapshots;
    for (const auto& feed : inputs->feeds) {
        auto snap = feed->load_snapshot();
        if (snap) snapshots[feed->venue()] = std::move(snap);
    }

    // Simulate fill per routing slice.
    std::vector<LegFillResult> leg_results;
    leg_results.reserve(routing.slices.size());
    for (const auto& slice : routing.slices) {
        auto it = snapshots.find(slice.venue);
        if (it == snapshots.end() || !it->second) {
            LegFillResult empty;
            empty.venue = slice.venue;
            leg_results.push_back(empty);
            continue;
        }
        double taker_fee = resolve_taker_fee(slice.venue, venue_runtime_info);
        leg_results.push_back(
            simulate_market_leg(*it->second, slice.venue, side, slice.quantity, taker_fee));
    }

    auto fill = aggregate_fills(leg_results, routing.requested_qty);

    // Write all simulated stage data (steps 3-6) to DB in a single transaction.
    try {
        pqxx::connection conn(supabase::with_connect_timeout(db_conn_str_));
        pqxx::work txn(conn);

        const std::string order_status = fill.fully_filled ? "filled" : "partially_filled";

        for (const auto& leg : fill.legs) {
            const std::string leg_status   = leg.fully_filled ? "filled" : "partially_filled";
            // Traceable sim ID without external dependency.
            const std::string sim_id = "SIM-" + order_id.substr(0, 8) + "-" + leg.venue;

            txn.exec(
                R"(
                    UPDATE public.order_legs
                    SET status             = $1,
                        quantity_submitted = $2,
                        price_submitted    = $3,
                        submitted_at       = NOW(),
                        acknowledged_at    = NOW(),
                        client_order_id    = $4,
                        venue_order_id     = $4,
                        quantity_filled    = $5,
                        price_filled_avg   = $6,
                        commission_usd     = $7,
                        first_fill_at      = NOW(),
                        last_fill_at       = NOW(),
                        terminal_at        = NOW(),
                        last_updated_at    = NOW()
                    WHERE order_id = $8 AND venue = $9
                )",
                pqxx::params(
                    leg_status,
                    leg.quantity_filled,
                    leg.avg_fill_price,
                    sim_id,
                    leg.quantity_filled,
                    leg.avg_fill_price,
                    leg.commission_usd,
                    order_id,
                    leg.venue));
        }

        txn.exec(
            R"(
                UPDATE public.orders
                SET status               = $1,
                    execution_started_at = NOW(),
                    terminal_at          = NOW(),
                    quantity_filled      = $2,
                    price_filled_avg     = $3,
                    total_commission_usd = $4,
                    last_updated_at      = NOW()
                WHERE id = $5
            )",
            pqxx::params(
                order_status,
                fill.total_quantity_filled,
                fill.weighted_avg_price,
                fill.total_commission_usd,
                order_id));

        txn.commit();
        return {std::move(fill), true, ""};
    } catch (const std::exception& e) {
        return {OrderFillResult{}, false, e.what()};
    }
}