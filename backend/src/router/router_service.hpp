#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <variant>

#include <pqxx/pqxx>

#include "server/feed_manager.hpp"
#include "router/order_router.hpp"

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
    RouterService(FeedManager& feeds, const std::string& db_conn_str)
        : feeds_(feeds), db_conn_str_(db_conn_str) {}

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
        
        /* ***********************************
         * CALCULATE ROUTING PATH
         ************************************/
        RoutingDecision routing = route_order_from_books(
            routing_inputs->feeds,
            req.side_lower,
            req.quantity_requested,
            req.limit_price
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
            pqxx::connection conn(db_conn_str_);
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
    FeedManager& feeds_;
    const std::string& db_conn_str_;
};
