#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include "router/router_common.hpp"
#include "venues/venue_api.hpp"
#include "server/feed_manager.hpp"

struct LimitOrderConfig {
    std::chrono::seconds      ttl          {60};   // order lifetime before expiry
    std::chrono::milliseconds poll_interval{250};  // book poll frequency while resting
};

class LimitExecutor {
public:
    LimitExecutor(
        FeedManager& feeds,
        const std::string& db_conn_str,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info,
        LimitOrderConfig config = {})
        : feeds_(feeds)
        , db_conn_str_(db_conn_str)
        , venue_static_info_(venue_static_info)
        , config_(config) {}

    // Launches a background thread and returns immediately.
    // The thread polls live orderbook data until filled, expired, or all legs rejected.
    void execute_async(
        std::string order_id,
        std::string symbol,
        std::string side,
        double limit_price,
        RoutingDecision routing,
        std::unordered_map<std::string, VenueRuntimeInfo> venue_runtime_info) const;

    void run(
        const std::string& order_id,
        const std::string& symbol,
        const std::string& side,
        double limit_price,
        const RoutingDecision& routing,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info) const;

    double resolve_fee(
        const std::string& venue,
        bool taker,
        const std::unordered_map<std::string, VenueRuntimeInfo>& runtime_info) const;

    FeedManager& feeds_;
    const std::string& db_conn_str_;
    const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info_;
    LimitOrderConfig config_;
};
