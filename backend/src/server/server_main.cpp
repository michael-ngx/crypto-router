#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>

#include "server/feed_manager.hpp"
#include "venues/venue_registry.hpp"
#include "server/pairs_config.hpp"
#include "server/venues_config.hpp"
#include "server/http_server.hpp"
#include "server/http_routes.hpp"
#include "supabase/storage_supabase.hpp"

using tcp = boost::asio::ip::tcp;

// Helper function to load .env file and set environment variables
void load_env_file(const std::string& filepath = ".env") {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Try in backend directory if not found
        std::string backend_path = "backend/" + filepath;
        file.open(backend_path);
        if (!file.is_open()) {
            return; // .env file not found, will use system env vars
        }
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Find the = sign
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Remove quotes if present
        if (!value.empty() && value[0] == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }
        if (!value.empty() && value[0] == '\'' && value.back() == '\'') {
            value = value.substr(1, value.length() - 2);
        }
        
        // Set environment variable (only if not already set)
        if (std::getenv(key.c_str()) == nullptr) {
            setenv(key.c_str(), value.c_str(), 0); // 0 = don't overwrite existing
        }
    }
}

// Helper function to get Supabase connection string from environment variables
std::string get_supabase_connection_string() {
    const char* db_url = std::getenv("SUPABASE_DB_URL");
    if (db_url) {
        return std::string(db_url);
    }
    
    // Alternative: build from individual components
    const char* host = std::getenv("SUPABASE_DB_HOST");
    const char* password = std::getenv("SUPABASE_DB_PASSWORD");
    const char* port = std::getenv("SUPABASE_DB_PORT");
    
    if (host && password) {
        std::string port_str = port ? std::string(port) : "5432";
        return "postgresql://postgres:" + std::string(password) + "@" + 
               std::string(host) + ":" + port_str + "/postgres?sslmode=require";
    }
    
    throw std::runtime_error(
        "Supabase connection string not found. "
        "Set SUPABASE_DB_URL or SUPABASE_DB_HOST + SUPABASE_DB_PASSWORD environment variables."
    );
}

std::vector<std::string> parse_csv_env(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return {};

    std::vector<std::string> out;
    std::stringstream ss{std::string(raw)};
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto start = item.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        auto end = item.find_last_not_of(" \t");
        item = item.substr(start, end - start + 1);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

int parse_env_int(const char* name, int fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    long val = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0') return fallback;
    if (val < 0) return fallback;
    return static_cast<int>(val);
}

bool parse_env_bool(const char* name, bool fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    std::string s(raw);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return fallback;
}

int main() {
    // Load .env file
    load_env_file();
    
    // Initialize Supabase connection and create tables
    std::unique_ptr<IOrderStore> order_store;
    std::string db_conn_str;
    try {
        db_conn_str = get_supabase_connection_string();
        order_store = make_supabase_store(db_conn_str);
        std::cout << "Database connected successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to initialize Supabase: " << e.what() << std::endl;
        std::cerr << "Server will continue without database functionality." << std::endl;
        db_conn_str = ""; // Empty string indicates no database
    }


    /*******************************************************************
    ***************************  Feed Manager **************************
     ********************************************************************/

    // Build VenueRuntime(s) for FeedManager. Each VenueRuntime includes the venue name, its factory, and an API instance
    const auto& registry = VenueRegistry::instance();
    std::vector<FeedManager::VenueRuntime> venues;
    venues.reserve(kVenueConfigs.size());
    for (const auto& venue_cfg : kVenueConfigs) {
        const VenueFactory* factory = registry.find(venue_cfg.name);
        if (!factory) {
            std::cerr << "[setup] Unknown venue '" << venue_cfg.name
                      << "'; skipping." << std::endl;
            continue;
        }

        auto api = factory->make_api ? factory->make_api() : nullptr;
        if (!api) {
            std::cerr << "[setup] Venue '" << venue_cfg.name
                      << "' did not provide an API implementation; skipping."
                      << std::endl;
            continue;
        }

        venues.push_back(FeedManager::VenueRuntime{venue_cfg.name, factory, std::move(api)});
    }

    // Parse FeedManager options from environment variables
    // If some options are missing or invalid, use defaults (e.g. empty hot pairs, 180s idle timeout, 15s sweep interval, no prewarm)
    FeedManager::Options feed_opts;
    feed_opts.hot_pairs = parse_csv_env("FEED_HOT_PAIRS");
    feed_opts.idle_timeout = std::chrono::seconds(parse_env_int("FEED_IDLE_SECONDS", 180));
    feed_opts.sweep_interval = std::chrono::seconds(parse_env_int("FEED_SWEEP_SECONDS", 15));
    feed_opts.prewarm_all = parse_env_bool("FEED_PREWARM_ALL", false);

    std::vector<std::string> canonical_pairs(kCanonicalPairs.begin(), kCanonicalPairs.end());  // All supporting canonical pairs from config
    bool prewarm_all = feed_opts.prewarm_all;

    // Create FeedManager instance
    FeedManager feed_manager(std::move(venues), std::move(canonical_pairs), std::move(feed_opts));

    if (prewarm_all) {
        feed_manager.start_all_supported();
    } else {
        feed_manager.start_hot();
    }


    /*******************************************************************
    ***************************  HTTP server **************************
     ********************************************************************/
    boost::asio::io_context ioc{1};
    tcp::endpoint ep{boost::asio::ip::make_address("0.0.0.0"), 8080};
    HttpServer server{ioc, ep, [&](auto const& req, auto& res){
      handle_request(feed_manager, db_conn_str, req, res);
    }};
    server.run();

    std::cout << "HTTP listening on :8080" << std::endl;
    std::cout << "Server started successfully" << std::endl;
    
    ioc.run();

    feed_manager.shutdown();
    return 0;
}
