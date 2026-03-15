#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include <pqxx/pqxx>

#include "server/feed_manager.hpp"
#include "router/router_framework.hpp"
#include "supabase/storage_supabase.hpp"
#include "venues/venue_api.hpp"

struct RouterOrderRequest {
    std::string user_id;
    std::string symbol;
    std::string side_lower; // "buy" | "sell"
    std::string type_lower; // "market" | "limit"
    double quantity_requested{0.0};
    std::optional<double> limit_price;
};

struct RouterOrderResult {
    std::string order_id;
    std::string status;
    RoutingDecision routing;
};

enum class RouterErrorCode {
    DatabaseNotConfigured,
    SymbolNotSupported,
    // Rare edge case: no resting liquidity on the relevant side across all venues.
    MarketNoLiquidity,
    InvalidRoutingPlan,
    DatabaseFailure,
};

struct RouterError {
    RouterErrorCode code;
    std::string message;
};

class RouterService {
public:
    RouterService(FeedManager& feeds,
                  const std::string& db_conn_str,
                  router::RouterVersionId router_version,
                  const std::unordered_map<std::string, VenueInfo>& venue_info)
        : feeds_(feeds),
          db_conn_str_(db_conn_str),
          router_version_(router_version),
          venue_info_(venue_info) {}

    std::variant<RouterOrderResult, RouterError> create_order(
        const RouterOrderRequest& req) const
    {
        if (db_conn_str_.empty()) {
            return RouterError{
                RouterErrorCode::DatabaseNotConfigured,
                "database not configured"
            };
        }

        auto routing_inputs = feeds_.acquire_routing_inputs(req.symbol);
        if (!routing_inputs) {
            return RouterError{
                RouterErrorCode::SymbolNotSupported,
                "symbol not supported"
            };
        }

        //? Fetch trailing volume EVERY ROUTING CALL
        std::unordered_map<std::string, double> trailing_volume_usd_by_venue;
        try {
            trailing_volume_usd_by_venue = fetch_user_trailing_volume_usd_by_venue(req.user_id);
        } catch (const std::exception& e) {
            return RouterError{
                RouterErrorCode::DatabaseFailure,
                std::string("failed to load user trailing volume: ") + e.what()
            };
        }
        
        /* ***********************************
         * CALCULATE ROUTING PATH
         ************************************/
        RoutingDecision routing = router::route_order(
            router_version_,
            routing_inputs->feeds,
            req.side_lower,
            req.quantity_requested,
            req.limit_price,
            venue_info_,
            trailing_volume_usd_by_venue
        );

        // For orders we require at least some immediately routable size.
        // This is to guard when the entire side is empty for market orders.
        // For limit orders we intentionally do NOT fail on liquidity; they remain open.
        constexpr double kEps = 1e-12;
        const bool has_routable_qty = routing.routable_qty > kEps;
        if (!has_routable_qty) {
            return RouterError{
                RouterErrorCode::MarketNoLiquidity,
                "order rejected: no liquidity on the book side across venues"
            };
        }
        
        // TODO: make sure routing logic is sane, so we can remove these lines
        // Defensive validation before persisting legs.
        if (routing.slices.empty()) {
            return RouterError{
                RouterErrorCode::InvalidRoutingPlan,
                "invalid routing plan: routable quantity has no legs"
            };
        }
        for (const auto& slice : routing.slices) {
            if (slice.quantity <= kEps || slice.price <= kEps) {
                return RouterError{
                    RouterErrorCode::InvalidRoutingPlan,
                    "invalid routing plan: leg quantity/price must be positive"
                };
            }
        }

        // Orders stay open until exchange execution reports arrive.
        // Routing outputs are indicative from the latest local snapshots.
        const std::string final_status = "open";
        const double quantity_planned = routing.routable_qty;
        const double price_planned_avg = routing.indicative_average_price;

        try {
            pqxx::connection conn(supabase::with_connect_timeout(db_conn_str_));
            pqxx::work txn(conn);
            pqxx::result result;

            /*
            * Log orders
            */
            if (req.limit_price.has_value()) {
                result = txn.exec(
                    R"(
                        INSERT INTO public.orders (
                            user_id, symbol, side, order_type,
                            quantity_requested, limit_price,
                            quantity_planned, price_planned_avg,
                            fully_routable, routing_message,
                            status, created_at, last_updated_at
                        )
                        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, NOW(), NOW())
                        RETURNING id
                    )",
                    pqxx::params(req.user_id, req.symbol, req.side_lower, req.type_lower,
                                 req.quantity_requested, *req.limit_price,
                                 quantity_planned, price_planned_avg,
                                 routing.fully_routable, routing.message,
                                 final_status)
                );
            } else {
                result = txn.exec(
                    R"(
                        INSERT INTO public.orders (
                            user_id, symbol, side, order_type,
                            quantity_requested, quantity_planned,
                            price_planned_avg, fully_routable,
                            routing_message, status, created_at, last_updated_at
                        )
                        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, NOW(), NOW())
                        RETURNING id
                    )",
                    pqxx::params(req.user_id, req.symbol, req.side_lower, req.type_lower,
                                 req.quantity_requested, quantity_planned,
                                 price_planned_avg, routing.fully_routable,
                                 routing.message, final_status)
                );
            }

            /*
            * Log orders_leg based on routing slices
            */
            std::string order_id = result[0][0].as<std::string>();
            
            for (const auto& slice : routing.slices) {
                if (req.limit_price.has_value()) {
                    txn.exec(
                        R"(
                            INSERT INTO public.order_legs (
                                order_id, venue, status, quantity_planned,
                                limit_price, price_planned,
                                quantity_filled, created_at, last_updated_at
                            )
                            VALUES ($1, $2, 'planned', $3, $4, $5, 0, NOW(), NOW())
                        )",
                        pqxx::params(order_id, slice.venue, slice.quantity,
                                     *req.limit_price, slice.price)
                    );
                } else {
                    txn.exec(
                        R"(
                            INSERT INTO public.order_legs (
                                order_id, venue, status, quantity_planned,
                                price_planned, quantity_filled,
                                created_at, last_updated_at
                            )
                            VALUES ($1, $2, 'planned', $3, $4, 0, NOW(), NOW())
                        )",
                        pqxx::params(order_id, slice.venue, slice.quantity, slice.price)
                    );
                }
            }

            // TODO(router-execution): Dispatch async execution job immediately after plan creation.
            // TODO(router-execution): Update orders/order_legs from actual exchange execution reports.

            txn.commit();
            return RouterOrderResult{
                order_id,
                final_status,
                std::move(routing),
            };
        } catch (const std::exception& e) {
            return RouterError{
                RouterErrorCode::DatabaseFailure,
                e.what()
            };
        }
    }

private:
    std::unordered_map<std::string, double> fetch_user_trailing_volume_usd_by_venue(
        const std::string& user_id) const
    {
        std::unordered_map<std::string, double> out;

        pqxx::connection conn(supabase::with_connect_timeout(db_conn_str_));
        pqxx::work txn(conn);

        // Approximate trailing venue volume using planned leg notional over the last 30 days.
        // This keeps fee tiers meaningful before live execution reporting is wired in.
        const auto result = txn.exec(
            R"(
                SELECT
                    ol.venue,
                    COALESCE(SUM((ol.quantity_planned * ol.price_planned)::double precision), 0.0)
                        AS trailing_volume_usd
                FROM public.order_legs ol
                JOIN public.orders o ON o.id = ol.order_id
                WHERE o.user_id = $1
                  AND o.created_at >= NOW() - INTERVAL '30 days'
                  AND o.status <> 'cancelled'
                  AND o.status <> 'failed'
                GROUP BY ol.venue
            )",
            pqxx::params(user_id)
        );

        out.reserve(result.size());
        for (const auto& row : result) {
            out.emplace(row[0].as<std::string>(), row[1].as<double>());
        }
        txn.commit();
        return out;
    }

    FeedManager& feeds_;
    const std::string& db_conn_str_;
    router::RouterVersionId router_version_;
    const std::unordered_map<std::string, VenueInfo>& venue_info_;
};
