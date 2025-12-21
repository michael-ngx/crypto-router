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

#include "ui/master_feed.hpp"
#include "venues/venue_api.hpp"
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

    // Build venue feeds for all configured pairs.
    struct VenueRuntime {
        std::string name;
        const VenueFactory* factory{nullptr};
        std::unique_ptr<IVenueApi> api;
    };

    const auto& registry = VenueRegistry::instance();
    std::vector<VenueRuntime> venues;
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

        venues.push_back(VenueRuntime{venue_cfg.name, factory, std::move(api)});
    }

    // Create UI master feeds for each trading pair, stored in a registry.
    UIFeedRegistry ui_feeds;
    std::vector<std::shared_ptr<IVenueFeed>> all_feeds;

    for (const auto& pair : kCanonicalPairs) {
        // Create master feed for this pair
        auto ui = std::make_shared<UIMasterFeed>(pair);
        bool registered = false;
        
        // Register venues that support this pair to the master feed
        for (const auto& venue : venues) {
            if (!venue.factory || !venue.api || !venue.api->supports_pair(pair)) {
                std::cerr << "[setup] " << venue.name
                          << " does not support " << pair
                          << "; skipping." << std::endl;
                continue;
            }

            auto feed = venue.factory->make_feed
                ? venue.factory->make_feed(pair)
                : nullptr;
            if (!feed) {
                std::cerr << "[setup] Venue '" << venue.name
                          << "' failed to create feed; skipping." << std::endl;
                continue;
            }

            const std::string venue_symbol =
                venue.factory->to_venue_symbol
                    ? venue.factory->to_venue_symbol(pair)
                    : pair;
            feed->start_ws(venue_symbol, 443);
            ui->add_feed(feed);   // Register to master feed
            all_feeds.push_back(feed);
            registered = true;
        }
        
        // If at least one venue registered, add to registry
        if (registered) {
            ui_feeds.emplace(pair, ui);
        } else {
            std::cerr << "[setup] No supported venues for " << pair
                      << "; not registering UI feed." << std::endl;
        }
    }


    // Start HTTP server
    boost::asio::io_context ioc{1};
    tcp::endpoint ep{boost::asio::ip::make_address("0.0.0.0"), 8080};
    HttpServer server{ioc, ep, [&](auto const& req, auto& res){
      handle_request(ui_feeds, db_conn_str, req, res);
    }};
    server.run();

    std::cout << "HTTP listening on :8080" << std::endl;
    std::cout << "Server started successfully" << std::endl;
    
    ioc.run();

    for (auto& feed : all_feeds) {
        if (feed) {
            feed->stop();
        }
    }
    return 0;
}
