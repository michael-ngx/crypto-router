#pragma once
#include <boost/url.hpp>
#include <boost/beast/http.hpp>
#include <string_view>
#include <string>
#include <optional>
#include <algorithm>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <vector>
#include "util/json_encode.hpp"
#include "ui/master_feed.hpp"
#include "server/feed_manager.hpp"
#include "supabase/auth_utils.hpp"
#include "order_book/storage.hpp"
#include <simdjson.h>
#include <pqxx/pqxx>

namespace http  = boost::beast::http;
namespace urls  = boost::urls;

// Handle /api/auth/signup endpoint
inline void handle_signup(const std::string& db_conn_str,
                          const std::string& request_body,
                          http::response<http::string_body>& res)
{
    if (db_conn_str.empty()) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"database not configured"})";
        return;
    }

    try {
        simdjson::padded_string pj(request_body);
        simdjson::ondemand::parser parser;
        auto doc_res = parser.iterate(pj);
        if (doc_res.error()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"invalid json"})";
            return;
        }
        auto doc = std::move(doc_res.value());

        std::string_view email_sv, password_sv, first_name_sv, last_name_sv;
        if (doc["email"].get(email_sv) || doc["password"].get(password_sv) ||
            doc["first_name"].get(first_name_sv) || doc["last_name"].get(last_name_sv)) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"missing required fields"})";
            return;
        }

        std::string email(email_sv);
        std::string password(password_sv);
        std::string first_name(first_name_sv);
        std::string last_name(last_name_sv);

        if (password.length() < 6) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"password must be at least 6 characters"})";
            return;
        }

        pqxx::connection conn(db_conn_str);
        pqxx::work txn(conn);

        // Check if user already exists
        auto check_result = txn.exec(
            "SELECT id FROM public.users WHERE email = $1",
            pqxx::params(email)
        );
        if (!check_result.empty()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"email already exists"})";
            return;
        }

        // Hash password and insert user
        std::string hashed_password = hash_password(password);
        auto result = txn.exec(
            R"(
                INSERT INTO public.users (email, password, first_name, last_name)
                VALUES ($1, $2, $3, $4)
                RETURNING id, email, first_name, last_name
            )",
            pqxx::params(email, hashed_password, first_name, last_name)
        );
        txn.commit();

        auto row = result[0];
        std::ostringstream os;
        os << "{"
           << "\"user_id\":\"" << row[0].as<std::string>() << "\","
           << "\"email\":\"" << json_escape(row[1].as<std::string>()) << "\","
           << "\"first_name\":\"" << json_escape(row[2].as<std::string>()) << "\","
           << "\"last_name\":\"" << json_escape(row[3].as<std::string>()) << "\""
           << "}";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
    } catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        std::ostringstream os;
        os << "{\"error\":\"" << json_escape(e.what()) << "\"}";
        res.body() = os.str();
    }
}

// Handle /api/auth/login endpoint
inline void handle_login(const std::string& db_conn_str,
                         const std::string& request_body,
                         http::response<http::string_body>& res)
{
    if (db_conn_str.empty()) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"database not configured"})";
        return;
    }

    try {
        simdjson::padded_string pj(request_body);
        simdjson::ondemand::parser parser;
        auto doc_res = parser.iterate(pj);
        if (doc_res.error()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"invalid json"})";
            return;
        }
        auto doc = std::move(doc_res.value());

        std::string_view email_sv, password_sv;
        if (doc["email"].get(email_sv) || doc["password"].get(password_sv)) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"missing email or password"})";
            return;
        }

        std::string email(email_sv);
        std::string password(password_sv);

        pqxx::connection conn(db_conn_str);
        pqxx::work txn(conn);

        auto result = txn.exec(
            R"(
                SELECT id, email, password, first_name, last_name
                FROM public.users
                WHERE email = $1
            )",
            pqxx::params(email)
        );

        if (result.empty()) {
            res.result(http::status::unauthorized);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"invalid email or password"})";
            return;
        }

        auto row = result[0];
        std::string stored_hash = row[2].as<std::string>();

        if (!verify_password(password, stored_hash)) {
            res.result(http::status::unauthorized);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"invalid email or password"})";
            return;
        }

        std::ostringstream os;
        os << "{"
           << "\"user_id\":\"" << row[0].as<std::string>() << "\","
           << "\"email\":\"" << json_escape(row[1].as<std::string>()) << "\","
           << "\"first_name\":\"" << json_escape(row[3].as<std::string>()) << "\","
           << "\"last_name\":\"" << json_escape(row[4].as<std::string>()) << "\""
           << "}";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
    } catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        std::ostringstream os;
        os << "{\"error\":\"" << json_escape(e.what()) << "\"}";
        res.body() = os.str();
    }
}

// Handle /api/orders POST endpoint
inline void handle_create_order(const std::string& db_conn_str,
                                const std::string& request_body,
                                http::response<http::string_body>& res)
{
    if (db_conn_str.empty()) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"database not configured"})";
        return;
    }

    try {
        simdjson::padded_string pj(request_body);
        simdjson::ondemand::parser parser;
        auto doc_res = parser.iterate(pj);
        if (doc_res.error()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"invalid json"})";
            return;
        }
        auto doc = std::move(doc_res.value());

        std::string_view user_id_sv, symbol_sv, side_sv, type_sv;

        // Check for required string fields
        if (doc["user_id"].get(user_id_sv) || doc["symbol"].get(symbol_sv) ||
            doc["side"].get(side_sv) || doc["type"].get(type_sv)) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"missing required fields"})";
            return;
        }

        // Get qty as a number (can be int or double)
        double quantity = 0.0;
        auto qty_val = doc["qty"];
        if (qty_val.error()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"missing qty field"})";
            return;
        }
        
        // Try to get as double (handles both int and double)
        if (qty_val.get_double().get(quantity)) {
            // If get_double fails, try get_int64
            std::int64_t qty_int = 0;
            if (qty_val.get_int64().get(qty_int)) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"error":"qty must be a number"})";
                return;
            }
            quantity = static_cast<double>(qty_int);
        }

        std::string user_id(user_id_sv);
        std::string symbol(symbol_sv);
        std::string side_str(side_sv);
        std::string type_str(type_sv);

        // Convert side to lowercase for database enum
        std::string side_lower = side_str;
        std::transform(side_lower.begin(), side_lower.end(), side_lower.begin(), ::tolower);

        // Convert type to lowercase for database enum
        std::string type_lower = type_str;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

        if (side_lower != "buy" && side_lower != "sell") {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"side must be 'buy' or 'sell'"})";
            return;
        }

        if (type_lower != "market" && type_lower != "limit") {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"type must be 'market' or 'limit'"})";
            return;
        }

        if (quantity <= 0) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"quantity must be positive"})";
            return;
        }

        std::optional<double> limit_price;
        if (type_lower == "limit") {
            auto price_val = doc["price"];
            if (price_val.error()) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"error":"limit orders require a price"})";
                return;
            }
            
            double price = 0.0;
            if (price_val.get_double().get(price)) {
                // Try as int64 if double fails
                std::int64_t price_int = 0;
                if (price_val.get_int64().get(price_int)) {
                    res.result(http::status::bad_request);
                    res.set(http::field::content_type, "application/json");
                    res.body() = R"({"error":"price must be a number"})";
                    return;
                }
                price = static_cast<double>(price_int);
            }
            
            if (price <= 0) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"error":"price must be positive"})";
                return;
            }
            limit_price = price;
        }

        pqxx::connection conn(db_conn_str);
        pqxx::work txn(conn);

        // Insert order into database
        std::string query;
        pqxx::result result;
        
        if (limit_price.has_value()) {
            query = R"(
                INSERT INTO public.orders (user_id, symbol, side, order_type, quantity, limit_price, status)
                VALUES ($1, $2, $3, $4, $5, $6, 'open')
                RETURNING id
            )";
            result = txn.exec(
                query,
                pqxx::params(user_id, symbol, side_lower, type_lower, quantity, *limit_price)
            );
        } else {
            query = R"(
                INSERT INTO public.orders (user_id, symbol, side, order_type, quantity, status)
                VALUES ($1, $2, $3, $4, $5, 'open')
                RETURNING id
            )";
            result = txn.exec(
                query,
                pqxx::params(user_id, symbol, side_lower, type_lower, quantity)
            );
        }

        txn.commit();

        auto row = result[0];
        std::ostringstream os;
        os << "{"
           << "\"order_id\":\"" << row[0].as<std::string>() << "\","
           << "\"status\":\"open\""
           << "}";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
    } catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        std::ostringstream os;
        os << "{\"error\":\"" << json_escape(e.what()) << "\"}";
        res.body() = os.str();
    }
}

// Handle /api/orders/:id PATCH endpoint (cancel an order)
inline void handle_cancel_order(const std::string& db_conn_str,
                                const std::string& order_id,
                                http::response<http::string_body>& res)
{
    if (db_conn_str.empty()) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"database not configured"})";
        return;
    }

    try {
        pqxx::connection conn(db_conn_str);
        pqxx::work txn(conn);

        // Check if order exists and is cancellable (open or partially_filled)
        std::string check_query = R"(
            SELECT id, status
            FROM public.orders
            WHERE id = $1
        )";

        auto check_result = txn.exec(check_query, pqxx::params(order_id));
        
        if (check_result.empty()) {
            res.result(http::status::not_found);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"order not found"})";
            return;
        }

        std::string current_status = check_result[0][1].as<std::string>();
        if (current_status != "open" && current_status != "partially_filled") {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"order cannot be cancelled"})";
            return;
        }

        // Update order status to cancelled and set closed_at
        std::string update_query = R"(
            UPDATE public.orders
            SET status = 'cancelled', closed_at = NOW()
            WHERE id = $1
            RETURNING id, status, closed_at
        )";

        auto result = txn.exec(update_query, pqxx::params(order_id));
        txn.commit();

        auto row = result[0];
        std::ostringstream os;
        os << "{"
           << "\"order_id\":\"" << row[0].as<std::string>() << "\","
           << "\"status\":\"" << json_escape(row[1].as<std::string>()) << "\","
           << "\"closed_at\":\"" << json_escape(row[2].as<std::string>()) << "\""
           << "}";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
    } catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        std::ostringstream os;
        os << "{\"error\":\"" << json_escape(e.what()) << "\"}";
        res.body() = os.str();
    }
}

// Handle /api/orders GET endpoint (fetch orders for a user)
inline void handle_get_orders(const std::string& db_conn_str,
                              const urls::url_view& url,
                              http::response<http::string_body>& res)
{
    if (db_conn_str.empty()) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"database not configured"})";
        return;
    }

    try {
        // Get user_id from query parameters
        std::string user_id;
        for (auto const& p : url.params()) {
            if (p.key == "user_id") {
                user_id = std::string(p.value);
                break;
            }
        }

        if (user_id.empty()) {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"user_id parameter required"})";
            return;
        }

        pqxx::connection conn(db_conn_str);
        pqxx::work txn(conn);

        // Fetch orders for the user, ordered by created_at descending
        std::string query = R"(
            SELECT id, symbol, side, order_type, quantity, limit_price, 
                   average_fill_price, status, created_at, closed_at
            FROM public.orders
            WHERE user_id = $1
            ORDER BY created_at DESC
        )";

        auto result = txn.exec(query, pqxx::params(user_id));

        std::ostringstream os;
        os << "{\"orders\":[";
        bool first = true;
        for (const auto& row : result) {
            if (!first) os << ",";
            first = false;

            os << "{"
               << "\"id\":\"" << row[0].as<std::string>() << "\","
               << "\"symbol\":\"" << json_escape(row[1].as<std::string>()) << "\","
               << "\"side\":\"" << json_escape(row[2].as<std::string>()) << "\","
               << "\"order_type\":\"" << json_escape(row[3].as<std::string>()) << "\","
               << "\"quantity\":" << row[4].as<double>() << ",";

            if (!row[5].is_null()) {
                os << "\"limit_price\":" << row[5].as<double>() << ",";
            } else {
                os << "\"limit_price\":null,";
            }

            if (!row[6].is_null()) {
                os << "\"average_fill_price\":" << row[6].as<double>() << ",";
            } else {
                os << "\"average_fill_price\":null,";
            }

            os << "\"status\":\"" << json_escape(row[7].as<std::string>()) << "\","
               << "\"created_at\":\"" << json_escape(row[8].as<std::string>()) << "\"";

            if (!row[9].is_null()) {
                os << ",\"closed_at\":\"" << json_escape(row[9].as<std::string>()) << "\"";
            } else {
                os << ",\"closed_at\":null";
            }

            os << "}";
        }
        os << "]}";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
    } catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "application/json");
        std::ostringstream os;
        os << "{\"error\":\"" << json_escape(e.what()) << "\"}";
        res.body() = os.str();
    }
}

// Handle /api/book endpoint
inline void handle_book(FeedManager& feeds,
                        const urls::url_view& url,
                        http::response<http::string_body>& res)
{
    std::size_t depth = MAX_TOP_DEPTH;
    std::string symbol;

    for (auto const& p : url.params()) {
        if (p.key == "depth") {
            try {
                std::size_t d = std::stoul(std::string(p.value));
                if (d > 0 && d <= MAX_TOP_DEPTH) {
                    depth = d;
                }
            } catch (...) {
                // ignore invalid input
            }
        } else if (p.key == "symbol") {
            symbol = std::string(p.value);
        }
    }

    if (symbol.empty()) {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"symbol parameter required"})";
        return;
    }

    auto ui = feeds.get_or_subscribe(symbol);
    if (!ui) {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"symbol not supported"})";
        return;
    }

    UIConsolidated snap = ui->snapshot_consolidated(depth);

    std::ostringstream os;
    if (snap.is_cold) {
        res.result(http::status::service_unavailable);
    } else {
        res.result(http::status::ok);
    }

    os << "{";
    os << "\"status\":{";
    os << "\"code\":" << (snap.is_cold ? 503 : 200) << ",";
    os << "\"message\":\""
       << (snap.is_cold ? "Market data stale: all venues cold" : "OK")
       << "\"";
    os << "},";
    if (snap.last_updated_ms > 0) {
        os << "\"last_updated_ms\":" << snap.last_updated_ms << ",";
    } else {
        os << "\"last_updated_ms\":null,";
    }
    os << "\"symbol\":\"" << json_escape(snap.symbol) << "\",";

    // Consolidated ladders with venue information for UI
    os << "\"bids\":"; json_ladder_array(os, snap.bids); os << ",";
    os << "\"asks\":"; json_ladder_array(os, snap.asks); os << ",";

    // Per-venue snapshots (still useful for debugging / side panels)
    os << "\"per_venue\":{";
    bool first = true;
    for (const auto& kv : snap.per_venue) {
        const auto& venue_name = kv.first;
        const auto& sp = kv.second;
        if (!sp) continue;

        if (!first) os << ",";
        first = false;

        os << "\"" << json_escape(venue_name) << "\":{";
        os << "\"venue\":\""  << json_escape(sp->venue)  << "\",";
        os << "\"symbol\":\"" << json_escape(sp->symbol) << "\",";
        os << "\"ts_ns\":"    << sp->ts_ns << ",";
        os << "\"bids\":"; json_pair_array(os, sp->bids); os << ",";
        os << "\"asks\":"; json_pair_array(os, sp->asks);
        os << "}";
    }
    os << "}"; // per_venue
    os << "}"; // root object

    res.set(http::field::content_type, "application/json");
    res.body() = os.str();
}

// Handle /api/pairs endpoint
inline void handle_pairs(const FeedManager& feeds,
                         http::response<http::string_body>& res)
{
    std::vector<std::string> pairs = feeds.list_supported_pairs();
    std::sort(pairs.begin(), pairs.end());

    std::ostringstream os;
    os << "{\"pairs\":[";
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) os << ",";
        os << "\"" << json_escape(pairs[i]) << "\"";
    }
    os << "]}";

    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = os.str();
}

inline void handle_request(FeedManager& feeds,
                           const std::string& db_conn_str,
                           const http::request<http::string_body>& req,
                           http::response<http::string_body>& res)
{
    res.set(http::field::server, "md-router/0.1");

    // Parse the target as an origin-form URL
    std::string_view target{req.target().data(), req.target().size()};
    auto parsed_result = urls::parse_origin_form(target);
    if (!parsed_result) {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"bad request"})";
        return;
    }

    urls::url_view url = *parsed_result;

    // /api/health
    if (req.method() == http::verb::get && url.path() == "/api/health") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"status":"ok"})";
        return;
    }

    // /api/pairs
    if (req.method() == http::verb::get && url.path() == "/api/pairs") {
        handle_pairs(feeds, res);
        return;
    }

    // /api/book?symbol=BTC-USD&depth=10
    if (req.method() == http::verb::get && url.path() == "/api/book") {
        handle_book(feeds, url, res);
        return;
    }

    // /api/auth/signup
    if (req.method() == http::verb::post && url.path() == "/api/auth/signup") {
        handle_signup(db_conn_str, req.body(), res);
        return;
    }

    // /api/auth/login
    if (req.method() == http::verb::post && url.path() == "/api/auth/login") {
        handle_login(db_conn_str, req.body(), res);
        return;
    }

    // /api/orders
    if (req.method() == http::verb::post && url.path() == "/api/orders") {
        handle_create_order(db_conn_str, req.body(), res);
        return;
    }

    // /api/orders?user_id=...
    if (req.method() == http::verb::get && url.path() == "/api/orders") {
        handle_get_orders(db_conn_str, url, res);
        return;
    }

    // /api/orders/:id PATCH endpoint (cancel order)
    std::string path(url.path());
    if (req.method() == http::verb::patch && path.starts_with("/api/orders/")) {
        std::string order_id(path.substr(12)); // Skip "/api/orders/"
        if (!order_id.empty()) {
            handle_cancel_order(db_conn_str, order_id, res);
            return;
        }
    }

    // 404
    res.result(http::status::not_found);
    res.set(http::field::content_type, "application/json");
    res.body() = R"({"error":"not found"})";
}
