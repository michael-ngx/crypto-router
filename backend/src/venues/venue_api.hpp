#pragma once

#include <string>
#include <vector>

/// A single tier in a volume-based fee ladder.
/// volume_threshold is the minimum trailing 30-day volume (in USD) required
/// to qualify for this tier.  Tiers are stored in ascending volume order.
struct FeeTier {
    double volume_threshold{0.0};   // USD trailing 30d volume lower bound
    double maker_fee{0.0};          // decimal fraction, e.g. 0.004 = 40 bps
    double taker_fee{0.0};          // decimal fraction
};

/// Complete fee schedule for a venue — a sorted ladder of volume-based tiers.
/// The first tier (index 0) is the base tier (volume_threshold == 0).
struct VenueFeeSchedule {
    std::vector<FeeTier> tiers;     // ascending by volume_threshold
    bool fetched_from_api{false};   // true if dynamically fetched from venue API

    /// Look up the maker/taker rates for a given trailing 30d USD volume.
    /// Walks the ladder top-down and returns the highest tier the volume qualifies for.
    /// If the ladder is empty, returns zero fees.
    FeeTier tier_for_volume(double trailing_volume_usd) const {
        // Walk from the highest tier downward.
        for (auto it = tiers.rbegin(); it != tiers.rend(); ++it) {
            if (trailing_volume_usd >= it->volume_threshold) {
                return *it;
            }
        }
        // Below the lowest tier (shouldn't happen if base tier starts at 0).
        if (!tiers.empty()) return tiers.front();
        return FeeTier{};
    }

    /// Convenience: return the base tier (lowest volume requirement).
    FeeTier base_tier() const {
        if (tiers.empty()) return FeeTier{};
        return tiers.front();
    }
};

/// Extensible container for venue-level metadata fetched at startup.
/// Currently holds fee schedule; future additions may include rate limits,
/// minimum order sizes, supported order types, etc.
struct VenueStaticInfo {
    VenueFeeSchedule fees;
};


/// @brief Container for venue-specific runtime info relevant to routing decisions
/// including trailing trade volume, latency, and volatility.
struct VenueRuntimeInfo {
    double trailing_volume_usd{0.0}; // trailing 30-day volume in USD, used for fee tier calculation
    double latency_ms{0.0};        // average round-trip latency to venue API in milliseconds
    double volatility{0.0};        // estimated price volatility for the venue's order book
};

class IVenueApi {
public:
    virtual ~IVenueApi() = default;

    virtual std::string name() const = 0;
    
    // Returns all supported canonical pairs for this venue (e.g. "BTC-USD").
    virtual std::vector<std::string> list_supported_pairs() const = 0;

    // Fetch venue-level metadata (fee schedule, etc.) from the venue's API.
    // Called once at application startup.  Implementations should try to fetch
    // from the venue's public API, falling back to documented base-tier rates.
    virtual VenueStaticInfo fetch_venue_static_info() const = 0;
};
