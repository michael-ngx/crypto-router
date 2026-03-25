#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
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
#include <string_view>

#include "server/feed_manager.hpp"
#include "venues/venue_registry.hpp"
#include "venues/venue_api.hpp"
#include "server/venues_config.hpp"
#include "server/http_server.hpp"
#include "server/http_routes.hpp"
#include "router/router_framework.hpp"
#include "supabase/storage_supabase.hpp"

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

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
        #ifdef _WIN32
                if (std::getenv(key.c_str()) == nullptr) {
                    _putenv_s(key.c_str(), value.c_str());
                }
        #else
                setenv(key.c_str(), value.c_str(), 0); // 0 = don't overwrite existing
        #endif
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

std::string parse_env_string(const char* name, const std::string& fallback = {}) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    return std::string(raw);
}

[[noreturn]] void fail_tls_configuration(const std::string& msg) {
    throw std::runtime_error(
        "TLS configuration error: " + msg +
        ". Set TLS_CERT_FILE and TLS_KEY_FILE to PEM files."
    );
}

int main() {
    // Load .env file
    load_env_file();
    
    // Initialize Supabase connection and create tables
    std::string db_conn_str;
    try {
        db_conn_str = get_supabase_connection_string();
        supabase::bootstrap_database(db_conn_str);
        std::cout << "Database connected and schema ready" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to initialize Supabase: " << e.what() << std::endl;
        std::cerr << "Server will continue without database functionality." << std::endl;
        db_conn_str = ""; // Empty string indicates no database
    }


    /* **********************************************
    * **************** Feed Manager *****************
    *************************************************
    */

    // Build VenueRuntime(s) for FeedManager. Each VenueRuntime includes the venue name, its factory, and an API instance
    // This is done based on the STATIC configuration in venues_config.hpp and STATIC registered factories in VenueRegistry
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

    // Fetch venue-level metadata (fee schedules, etc.) from each venue at startup.
    // This map is immutable after construction and shared across all request handlers.
    std::unordered_map<std::string, VenueStaticInfo> venue_static_info;
    for (const auto& venue : venues) {
        if (venue.api) {
            venue_static_info.emplace(venue.name, venue.api->fetch_venue_static_info());
        }
    }

    // Parse FeedManager options from environment variables
    // If some options are missing or invalid, use defaults (e.g. empty hot pairs, 180s idle timeout, 15s sweep interval, no prewarm all)
    FeedManager::Options feed_opts;
    feed_opts.hot_pairs = parse_csv_env("FEED_HOT_PAIRS");
    feed_opts.idle_timeout = std::chrono::seconds(parse_env_int("FEED_IDLE_SECONDS", 180));
    feed_opts.sweep_interval = std::chrono::seconds(parse_env_int("FEED_SWEEP_SECONDS", 15));
    feed_opts.prewarm_all = parse_env_bool("FEED_PREWARM_ALL", false);

    bool prewarm_all = feed_opts.prewarm_all;

    const char* router_version_env = std::getenv("ROUTER_VERSION");
    const std::string_view requested_router_version =
        router_version_env ? std::string_view(router_version_env)
                           : std::string_view{};

    if (router_version_env &&
        !router::is_router_version_supported(requested_router_version)) {
        std::cerr << "[router] Unknown ROUTER_VERSION='" << router_version_env
                  << "', falling back to default strategy." << std::endl;
    }
    const router::RouterVersionId router_version = router::resolve_router_version_id(requested_router_version);

    // Create FeedManager instance, and START
    FeedManager feed_manager(std::move(venues), std::move(feed_opts));

    if (prewarm_all) {
        feed_manager.start_all_supported();
    } else {
        feed_manager.start_hot();
    }


    /* **********************************************
    * **************** HTTPS Server *****************
    *************************************************
    */
    const std::string tls_cert_file = parse_env_string("TLS_CERT_FILE");
    const std::string tls_key_file = parse_env_string("TLS_KEY_FILE");
    if (tls_cert_file.empty()) fail_tls_configuration("TLS_CERT_FILE is missing");
    if (tls_key_file.empty()) fail_tls_configuration("TLS_KEY_FILE is missing");

    const std::string bind_address = parse_env_string("HTTPS_BIND_ADDRESS", "0.0.0.0");
    const int https_port = parse_env_int("HTTPS_PORT", 8443);
    if (https_port <= 0 || https_port > 65535) {
        throw std::runtime_error("Invalid HTTPS_PORT value. Must be between 1 and 65535.");
    }

    boost::asio::io_context ioc{1};   // TODO: Make multiple HTTP threads
    ssl::context ssl_ctx{ssl::context::tls_server};
    ssl_ctx.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3 |
        ssl::context::no_tlsv1 |
        ssl::context::no_tlsv1_1
    );
    ssl_ctx.use_certificate_chain_file(tls_cert_file);
    ssl_ctx.use_private_key_file(tls_key_file, ssl::context::file_format::pem);

    tcp::endpoint ep{
        boost::asio::ip::make_address(bind_address),
        static_cast<unsigned short>(https_port)
    };
    std::cout << "[router] Active strategy: "
              << router::router_version_name(router_version)
              << std::endl;
    HttpServer server{ioc, ssl_ctx, ep, [&](auto const& req, auto& res){
      handle_request(feed_manager, db_conn_str, router_version, venue_static_info, req, res);
    }};
    server.run();

    std::cout << "HTTPS listening on " << bind_address << ":" << https_port << std::endl;
    std::cout << "Server started successfully" << std::endl;
    
    ioc.run();

    feed_manager.shutdown();
    return 0;
}
