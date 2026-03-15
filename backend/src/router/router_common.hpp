#pragma once

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "md/venue_feed_iface.hpp"

struct RouteSlice {
    std::string venue;
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
