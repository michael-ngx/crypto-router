#pragma once
#include <string>
#include <unordered_map>
#include "router/router_common.hpp"
#include "venues/venue_api.hpp"
#include "server/feed_manager.hpp"
#include "execution/fill_simulator.hpp"

struct MarketExecutionResult {
    OrderFillResult fill;
    bool ok{false};
    std::string error;
};

class MarketExecutor {
public:
    MarketExecutor(
        FeedManager& feeds,
        const std::string& db_conn_str,
        const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info)
        : feeds_(feeds)
        , db_conn_str_(db_conn_str)
        , venue_static_info_(venue_static_info) {}

    // Simulate fills for all routing slices and write results to DB.
    MarketExecutionResult execute(
        const std::string& order_id,
        const std::string& symbol,
        const std::string& side,
        const RoutingDecision& routing,
        const std::unordered_map<std::string, VenueRuntimeInfo>& venue_runtime_info) const;

private:
    // Resolve taker fee for a venue given the user's trailing volume.
    double resolve_taker_fee(
        const std::string& venue,
        const std::unordered_map<std::string, VenueRuntimeInfo>& runtime_info) const;

    void ensure_schema() const;

    FeedManager& feeds_;
    const std::string& db_conn_str_;
    const std::unordered_map<std::string, VenueStaticInfo>& venue_static_info_;
};