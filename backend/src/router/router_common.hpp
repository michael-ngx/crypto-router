#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <algorithm>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>

#include "md/venue_feed_iface.hpp"

enum ExecutionType {
    MARKET,
    LIMIT_POST_ONLY,
    LIMIT_ALLOW_TAKER
};

struct RouteSlice {
    std::string venue;
    ExecutionType execution_type;
    // Aggregated planned amount, and planned average execution price for this venue leg.
    double quantity{0.0};
    double price{0.0};
};

struct RoutingDecision {
    bool fully_routable{false};
    double requested_qty{0.0};
    double routable_qty{0.0};
    double indicative_average_price{0.0};
    std::vector<RouteSlice> slices;
    std::string message;
};

inline constexpr double kRoutingEps = 1e-12;

inline void set_routing_message(
    RoutingDecision& out,
    const std::optional<double>& limit_price)
{
    if (out.routable_qty <= kRoutingEps) {
        out.message = limit_price.has_value()
            ? "no liquidity matched the limit price"
            : "no liquidity available";
    } else if (out.fully_routable) {
        out.message = "fully routable from current snapshots";
    } else {
        out.message = limit_price.has_value()
            ? "partially routable: limit-constrained liquidity"
            : "partially routable: insufficient liquidity";
    }
}
