#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

#include "ws/ws.hpp"
#include "md/symbol_codec.hpp"
#include "md/book_parser_coinbase.hpp"
#include "md/book_parser_kraken.hpp"

#include "pipeline/venue_feed.hpp"
#include "pipeline/master_feed.hpp"
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
    // Load .env file first (if it exists)
    load_env_file();
    
    // Initialize Supabase connection and create tables
    std::unique_ptr<IOrderStore> order_store;
    try {
        std::string conn_str = get_supabase_connection_string();
        order_store = make_supabase_store(conn_str);
        std::cout << "Database connected successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to initialize Supabase: " << e.what() << std::endl;
        std::cerr << "Server will continue without database functionality." << std::endl;
    }
    // Create VenueFeeds for coinbase and kraken BTC-USD
    using CbFeed = VenueFeed<CoinbaseWs, CoinbaseBookParser>;
    using KrFeed = VenueFeed<KrakenWs,  KrakenBookParser>;

    const std::string canonical = "BTC-USD";
    auto cb = std::make_shared<CbFeed>("Coinbase", canonical, Backpressure::DropOldest, MAX_TOP_DEPTH);
    auto kr = std::make_shared<KrFeed>("Kraken",  canonical, Backpressure::DropOldest, MAX_TOP_DEPTH);

    cb->start_ws(SymbolCodec::to_venue("Coinbase", canonical), 443);
    kr->start_ws(SymbolCodec::to_venue("Kraken",  canonical), 443);


    // Create UIMasterFeed and register venue feeds
    UIMasterFeed ui{canonical};
    ui.add_feed(cb);
    ui.add_feed(kr);


    // Start HTTP server
    boost::asio::io_context ioc{1};
    tcp::endpoint ep{boost::asio::ip::make_address("0.0.0.0"), 8080};
    HttpServer server{ioc, ep, [&](auto const& req, auto& res){
      handle_request(ui, req, res);
    }};
    server.run();

    std::cout << "HTTP listening on :8080" << std::endl;
    std::cout << "Server started successfully" << std::endl;
    
    ioc.run();

    kr->stop();
    cb->stop();
    return 0;
}