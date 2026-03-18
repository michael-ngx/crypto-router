#pragma once
#include <string>
#include <vector>
#include "md/book_snapshot.hpp"

struct LegFillResult {
    std::string venue;
    double quantity_filled{0.0};
    double avg_fill_price{0.0};
    double total_notional{0.0};
    double commission_usd{0.0};
    double fee_rate{0.0};
    int levels_consumed{0};
    bool fully_filled{false};
};

struct OrderFillResult {
    std::vector<LegFillResult> legs;
    double total_quantity_filled{0.0};
    double weighted_avg_price{0.0};
    double total_commission_usd{0.0};
    bool fully_filled{false};
};

// Walk the book for one venue leg. side="buy" walks asks, side="sell" walks bids.
LegFillResult simulate_market_leg(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double taker_fee_rate);

// Returns true if limit_price would cross the spread on arrival.
bool crosses_spread(const BookSnapshot& snapshot,
                    const std::string& side,
                    double limit_price);

// Walk levels at or better than limit_price, up to quantity.
// fee_rate is taker on arrival crossing, maker on resting fill.
LegFillResult simulate_limit_fill(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double limit_price,
    double fee_rate);

// For a resting maker order: fills at exactly limit_price.
// Uses opposite-side depth only to determine available quantity.
LegFillResult simulate_resting_fill(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double limit_price,
    double maker_fee_rate);

OrderFillResult aggregate_fills(
    const std::vector<LegFillResult>& legs,
    double requested_qty);
    