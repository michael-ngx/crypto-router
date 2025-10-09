#pragma once
#include <string>
#include <cstdint>
#include <chrono>

enum class Side { BUY, SELL };

enum class OrderType { LIMIT, MARKET };

enum class OrderStatus { NEW, CANCELED, FILLED, PARTIALLY_FILLED };

inline const char* to_cstr(Side s){ return s==Side::BUY?"BUY":"SELL"; }
inline const char* to_cstr(OrderType t){ return t==OrderType::LIMIT?"LIMIT":"MARKET"; }
inline const char* to_cstr(OrderStatus st){
    switch(st){
        case OrderStatus::NEW: return "NEW";
        case OrderStatus::CANCELED: return "CANCELED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
    }
    return "?";
}

struct Order {
    std::string id;      // assigned by store, e.g., ord-1
    std::string symbol;  // e.g., BTC-USD
    Side side{Side::BUY};
    OrderType type{OrderType::LIMIT};
    double price{0.0};   // limit price (ignored for MARKET)
    double qty{0.0};
    OrderStatus status{OrderStatus::NEW};
    std::int64_t ts_ns{0}; // created at (monotonic since start)
};