#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "router/router_common.hpp"
#include "router/versions/all_versions.hpp"
#include "venues/venue_api.hpp"

namespace router {

inline constexpr std::string_view kRouterV1 = "v1_best_price_sweep";
inline constexpr std::string_view kRouterV2 = "v2_best_price_fee";
inline constexpr std::size_t kRouterVersionCount = 2;

enum class RouterVersionId : std::uint8_t {
    V1BestPriceSweep = 1,
    V2BestPriceFee = 2,
};
inline constexpr RouterVersionId kDefaultRouterVersionId = RouterVersionId::V1BestPriceSweep;

inline std::string_view router_version_name(RouterVersionId version_id) {
    switch (version_id) {
        case RouterVersionId::V2BestPriceFee:
            return kRouterV2;
        case RouterVersionId::V1BestPriceSweep:
        default:
            return kRouterV1;
    }
}

inline std::optional<RouterVersionId> parse_exact_router_version(
    std::string_view requested_version)
{
    if (requested_version == kRouterV1) {
        return RouterVersionId::V1BestPriceSweep;
    }
    if (requested_version == kRouterV2) {
        return RouterVersionId::V2BestPriceFee;
    }
    return std::nullopt;
}


inline RouterVersionId resolve_router_version_id(std::string_view requested_version) {
    if (requested_version.empty()) {
        return kDefaultRouterVersionId;
    }

    if (const auto exact = parse_exact_router_version(requested_version)) {
        return *exact;
    }
    return kDefaultRouterVersionId;
}

inline bool is_router_version_supported(std::string_view requested_version) {
    if (requested_version.empty()) return true;
    return parse_exact_router_version(requested_version).has_value();
}

inline RoutingDecision route_order(
    RouterVersionId version_id,
    const std::vector<std::shared_ptr<IVenueFeed>>& feeds,
    const std::string& side_lower,
    double quantity,
    const std::optional<double>& limit_price,
    const std::unordered_map<std::string, VenueInfo>& venue_info,
    const std::unordered_map<std::string, double>& trailing_volume_usd_by_venue)
{
    RoutingDecision out;
    switch (version_id) {
        case RouterVersionId::V2BestPriceFee:
            out = RouterV2BestPriceFee::route_order(feeds, side_lower, quantity, limit_price, venue_info, trailing_volume_usd_by_venue);
            return out;
        case RouterVersionId::V1BestPriceSweep:
        default:
            out = RouterV1BestPriceSweep::route_order(feeds, side_lower, quantity, limit_price);
            return out;
    }
}

} // namespace router
