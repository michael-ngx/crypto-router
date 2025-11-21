#pragma once
#include "order_book/storage.hpp"
#include <pqxx/pqxx>
#include <memory>
#include <string>
#include <exception>
#include <fstream>
#include <sstream>
#include <iostream>

// Supabase-backed order store using PostgreSQL via libpqxx
class SupabaseOrderStore : public IOrderStore {
public:
    // Connection string format:
    // "postgresql://postgres:[PASSWORD]@db.[PROJECT_REF].supabase.co:5432/postgres?sslmode=require"
    explicit SupabaseOrderStore(const std::string& connection_string)
        : conn_str_(connection_string) {
        // Add connection timeout to connection string if not present
        std::string conn_with_timeout = conn_str_;
        if (conn_with_timeout.find("connect_timeout") == std::string::npos) {
            if (conn_with_timeout.find("?") != std::string::npos) {
                conn_with_timeout += "&connect_timeout=10";
            } else {
                conn_with_timeout += "?connect_timeout=10";
            }
        }
        
        // Test connection and initialize schema on construction
        auto test_conn = std::make_unique<pqxx::connection>(conn_with_timeout);
        ensure_schema(*test_conn);
    }

    std::string add(const Order& order) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);

        Order o = order;
        if (o.id.empty()) {
            o.id = generate_id();
        }
        if (o.ts_ns == 0) {
            o.ts_ns = now_ns();
        }

        // Insert order into database
        std::string query = R"(
            INSERT INTO orders (id, symbol, side, order_type, price, qty, status, ts_ns, user_id)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            RETURNING id
        )";

        try {
            pqxx::result result = txn.exec(
                query,
                pqxx::params(
                    o.id,
                    o.symbol,
                    static_cast<int>(o.side),
                    static_cast<int>(o.type),
                    o.price,
                    o.qty,
                    static_cast<int>(o.status),
                    o.ts_ns,
                    o.user_id.empty() ? pqxx::zview(nullptr) : o.user_id
                )
            );
            txn.commit();
            return result[0][0].as<std::string>();
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to add order: " + std::string(e.what()));
        }
    }

    std::optional<Order> get(const std::string& id) const override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);

        std::string query = R"(
            SELECT id, symbol, side, order_type, price, qty, status, ts_ns, user_id
            FROM orders
            WHERE id = $1
        )";

        try {
            pqxx::result result = txn.exec(query, pqxx::params(id));
            if (result.empty()) {
                return std::nullopt;
            }

            auto row = result[0];
            Order o;
            o.id = row[0].as<std::string>();
            o.symbol = row[1].as<std::string>();
            o.side = static_cast<Side>(row[2].as<int>());
            o.type = static_cast<OrderType>(row[3].as<int>());
            o.price = row[4].as<double>();
            o.qty = row[5].as<double>();
            o.status = static_cast<OrderStatus>(row[6].as<int>());
            o.ts_ns = row[7].as<std::int64_t>();
            if (!row[8].is_null()) {
                o.user_id = row[8].as<std::string>();
            }
            return o;
        } catch (const std::exception& e) {
            return std::nullopt;
        }
    }

    std::vector<Order> list() const override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);

        std::string query = R"(
            SELECT id, symbol, side, order_type, price, qty, status, ts_ns, user_id
            FROM orders
            ORDER BY ts_ns DESC
        )";

        try {
            pqxx::result result = txn.exec(query);
            std::vector<Order> orders;
            orders.reserve(result.size());

            for (const auto& row : result) {
                Order o;
                o.id = row[0].as<std::string>();
                o.symbol = row[1].as<std::string>();
                o.side = static_cast<Side>(row[2].as<int>());
                o.type = static_cast<OrderType>(row[3].as<int>());
                o.price = row[4].as<double>();
                o.qty = row[5].as<double>();
                o.status = static_cast<OrderStatus>(row[6].as<int>());
                o.ts_ns = row[7].as<std::int64_t>();
                if (!row[8].is_null()) {
                    o.user_id = row[8].as<std::string>();
                }
                orders.push_back(o);
            }
            return orders;
        } catch (const std::exception& e) {
            return {};
        }
    }

    bool cancel(const std::string& id) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);

        std::string query = R"(
            UPDATE orders
            SET status = $1
            WHERE id = $2
            AND status IN ($3, $4)
            RETURNING id
        )";

        try {
            pqxx::result result = txn.exec(
                query,
                pqxx::params(
                    static_cast<int>(OrderStatus::CANCELED),
                    id,
                    static_cast<int>(OrderStatus::NEW),
                    static_cast<int>(OrderStatus::PARTIALLY_FILLED)
                )
            );
            txn.commit();
            return !result.empty();
        } catch (const std::exception& e) {
            return false;
        }
    }

    bool update_status(const std::string& id, OrderStatus status) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);

        std::string query = R"(
            UPDATE orders
            SET status = $1
            WHERE id = $2
            RETURNING id
        )";

        try {
            pqxx::result result = txn.exec(
                query,
                pqxx::params(static_cast<int>(status), id)
            );
            txn.commit();
            return !result.empty();
        } catch (const std::exception& e) {
            return false;
        }
    }

private:
    std::string conn_str_;

    static std::int64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count();
    }

    std::string generate_id() {
        static std::uint64_t counter = 0;
        return "ORD-" + std::to_string(now_ns()) + "-" + std::to_string(++counter);
    }

    static std::string read_sql_file(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open SQL file: " + filepath);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void ensure_schema(pqxx::connection& conn) {
        // Use nontransaction for DDL statements (CREATE TABLE, etc.)
        pqxx::nontransaction ntxn(conn);
        
        try {
            // Read SQL from file
            std::string sql;
            try {
                sql = read_sql_file("src/supabase/schema/build_tables.sql");
            } catch (const std::exception& e1) {
                // Try absolute path as fallback
                try {
                    sql = read_sql_file("/Users/christophershih/Documents/GitHub/crypto-router/backend/src/supabase/schema/build_tables.sql");
                } catch (const std::exception& e2) {
                    throw std::runtime_error("Failed to read SQL file: " + std::string(e1.what()) + " / " + std::string(e2.what()));
                }
            }
            
            // Split SQL by semicolon and execute each statement
            std::istringstream stream(sql);
            std::string statement;
            std::string line;
            
            while (std::getline(stream, line)) {
                // Skip comment-only lines
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.empty() || trimmed.find("--") == 0) {
                    continue;
                }
                
                statement += line + "\n";
                
                // Execute when we hit a semicolon
                if (line.find(';') != std::string::npos) {
                    // Remove trailing whitespace
                    while (!statement.empty() && 
                           (statement.back() == ' ' || statement.back() == '\t' || 
                            statement.back() == '\n' || statement.back() == '\r')) {
                        statement.pop_back();
                    }
                    
                    if (!statement.empty()) {
                        try {
                            auto result = ntxn.exec(statement);  // Use exec() for DDL
                            (void)result;  // Result not needed for DDL statements
                        } catch (const pqxx::sql_error& e) {
                            // Ignore "already exists" errors (IF NOT EXISTS handles this)
                            std::string err_msg = e.what();
                            if (err_msg.find("already exists") == std::string::npos &&
                                err_msg.find("duplicate key") == std::string::npos) {
                                // Log unexpected errors but don't fail
                                std::cerr << "Schema execution warning: " << err_msg << std::endl;
                            }
                        }
                    }
                    statement.clear();
                }
            }
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to execute schema SQL: " + std::string(e.what()));
        }
    }
};

inline std::unique_ptr<IOrderStore> make_supabase_store(const std::string& connection_string) {
    return std::make_unique<SupabaseOrderStore>(connection_string);
}

