#pragma once

#include <pqxx/pqxx>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace supabase {

inline std::string with_connect_timeout(std::string connection_string,
                                        int timeout_seconds = 10) {
    if (timeout_seconds <= 0 ||
        connection_string.find("connect_timeout=") != std::string::npos) {
        return connection_string;
    }

    connection_string +=
        (connection_string.find('?') == std::string::npos) ? '?' : '&';
    connection_string += "connect_timeout=" + std::to_string(timeout_seconds);
    return connection_string;
}

namespace detail {

inline std::filesystem::path resolve_schema_path() {
    constexpr std::array<std::string_view, 2> candidates{
        "src/supabase/schema/build_tables.sql",
        "backend/src/supabase/schema/build_tables.sql",
    };

    for (std::string_view candidate : candidates) {
        std::filesystem::path path(candidate);
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    std::ostringstream os;
    os << "Failed to locate schema SQL file. Tried:";
    for (std::string_view candidate : candidates) {
        os << " " << candidate;
    }
    throw std::runtime_error(os.str());
}

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SQL file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline std::vector<std::string> split_sql_statements(const std::string& sql) {
    std::vector<std::string> statements;

    std::istringstream stream(sql);
    std::string statement;
    std::string line;
    while (std::getline(stream, line)) {
        const auto first_non_ws = line.find_first_not_of(" \t");
        if (first_non_ws == std::string::npos) {
            continue;
        }

        if (line.compare(first_non_ws, 2, "--") == 0) {
            continue;
        }

        statement += line;
        statement.push_back('\n');

        if (line.find(';') != std::string::npos) {
            while (!statement.empty() &&
                   (statement.back() == ' ' || statement.back() == '\t' ||
                    statement.back() == '\n' || statement.back() == '\r')) {
                statement.pop_back();
            }

            if (!statement.empty()) {
                statements.push_back(statement);
            }
            statement.clear();
        }
    }

    while (!statement.empty() &&
           (statement.back() == ' ' || statement.back() == '\t' ||
            statement.back() == '\n' || statement.back() == '\r')) {
        statement.pop_back();
    }
    if (!statement.empty()) {
        statements.push_back(statement);
    }

    return statements;
}

inline void apply_schema(pqxx::connection& conn, const std::string& schema_sql) {
    pqxx::nontransaction txn(conn);
    for (const std::string& statement : split_sql_statements(schema_sql)) {
        try {
            txn.exec(statement);
        } catch (const pqxx::sql_error& e) {
            const std::string msg = e.what();
            if (msg.find("already exists") != std::string::npos ||
                msg.find("duplicate key") != std::string::npos) {
                continue;
            }
            throw;
        }
    }
}

}  // namespace detail

inline void bootstrap_database(const std::string& connection_string) {
    const std::string conn_with_timeout = with_connect_timeout(connection_string);
    pqxx::connection conn(conn_with_timeout);

    const std::filesystem::path schema_path = detail::resolve_schema_path();
    const std::string schema_sql = detail::read_text_file(schema_path);
    detail::apply_schema(conn, schema_sql);
}

}  // namespace supabase
