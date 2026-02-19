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
    double quantity{0.0};
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
            req.quantity,
            req.limit_price
        );

        // For market orders we require at least some immediately routable size.
        // This is rare in liquid markets, but possible when the side is empty.
        // For limit orders we intentionally do NOT fail on liquidity; they remain open.
        const bool has_routable_qty = routing.routable_qty > 0.0;
        if (req.type_lower == "market" && !has_routable_qty) {
            return RouterError{
                RouterErrorCode::MarketNoLiquidity,
                "market order rejected: no liquidity on the book side across venues"
            };
        }

        // Orders stay open until real exchange execution reports arrive.
        // Routing outputs are indicative from the latest local snapshots.
        const std::string final_status = "open";

        try {
            pqxx::connection conn(db_conn_str_);
            pqxx::work txn(conn);

            pqxx::result result;
            if (req.limit_price.has_value()) {
                result = txn.exec(
                    R"(
                        INSERT INTO public.orders (
                            user_id, symbol, side, order_type, quantity,
                            limit_price, status
                        )
                        VALUES ($1, $2, $3, $4, $5, $6, $7)
                        RETURNING id
                    )",
                    pqxx::params(req.user_id, req.symbol, req.side_lower, req.type_lower,
                                 req.quantity, *req.limit_price, final_status)
                );
            } else {
                result = txn.exec(
                    R"(
                        INSERT INTO public.orders (
                            user_id, symbol, side, order_type, quantity, status
                        )
                        VALUES ($1, $2, $3, $4, $5, $6)
                        RETURNING id
                    )",
                    pqxx::params(req.user_id, req.symbol, req.side_lower, req.type_lower,
                                 req.quantity, final_status)
                );
            }

            std::string order_id = result[0][0].as<std::string>();

            // TODO(router-execution): Dispatch async execution job immediately after plan creation.
            // TODO(router-execution): Update orders/order_breakdown only from actual exchange execution reports.

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
