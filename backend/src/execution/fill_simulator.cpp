#include "fill_simulator.hpp"
#include <algorithm>
#include <cmath>

LegFillResult simulate_market_leg(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double taker_fee_rate)
{
    LegFillResult result;
    result.venue    = venue;
    result.fee_rate = taker_fee_rate;

    const auto& levels = (side == "buy") ? snapshot.asks : snapshot.bids;
    if (levels.empty()) return result;

    double remaining = quantity;
    for (const auto& lvl : levels) {
        if (remaining <= 1e-12) break;
        double take_qty        = std::min(remaining, lvl.size);
        result.total_notional  += take_qty * lvl.price;
        result.quantity_filled += take_qty;
        remaining              -= take_qty;
        ++result.levels_consumed;
    }

    result.fully_filled = (remaining <= 1e-12);
    if (result.quantity_filled > 1e-12)
        result.avg_fill_price = result.total_notional / result.quantity_filled;
    result.commission_usd = result.total_notional * taker_fee_rate;
    return result;
}

bool crosses_spread(const BookSnapshot& snapshot,
                    const std::string& side,
                    double limit_price)
{
    if (side == "buy")
        return !snapshot.asks.empty() && snapshot.asks.front().price <= limit_price;
    else
        return !snapshot.bids.empty() && snapshot.bids.front().price >= limit_price;
}

LegFillResult simulate_limit_fill(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double limit_price,
    double fee_rate)
{
    LegFillResult result;
    result.venue    = venue;
    result.fee_rate = fee_rate;

    const auto& levels = (side == "buy") ? snapshot.asks : snapshot.bids;
    if (levels.empty()) return result;

    double remaining = quantity;
    for (const auto& lvl : levels) {
        if (remaining <= 1e-12) break;
        // Only fill levels at or better than the limit price
        if (side == "buy"  && lvl.price > limit_price) break;
        if (side == "sell" && lvl.price < limit_price) break;

        double take_qty        = std::min(remaining, lvl.size);
        result.total_notional  += take_qty * lvl.price;
        result.quantity_filled += take_qty;
        remaining              -= take_qty;
        ++result.levels_consumed;
    }

    result.fully_filled = (remaining <= 1e-12);
    if (result.quantity_filled > 1e-12)
        result.avg_fill_price = result.total_notional / result.quantity_filled;
    result.commission_usd = result.total_notional * fee_rate;
    return result;
}

LegFillResult simulate_resting_fill(
    const BookSnapshot& snapshot,
    const std::string& venue,
    const std::string& side,
    double quantity,
    double limit_price,
    double maker_fee_rate)
{
    LegFillResult result;
    result.venue    = venue;
    result.fee_rate = maker_fee_rate;

    // Use opposite-side depth to estimate available quantity, but fill at
    // limit_price — resting makers don't get price improvement.
    const auto& levels = (side == "buy") ? snapshot.asks : snapshot.bids;
    if (levels.empty()) return result;

    double available = 0.0;
    for (const auto& lvl : levels) {
        if (side == "buy"  && lvl.price > limit_price) break;
        if (side == "sell" && lvl.price < limit_price) break;
        available += lvl.size;
        ++result.levels_consumed;
    }

    result.quantity_filled = std::min(quantity, available);
    result.fully_filled    = (result.quantity_filled >= quantity - 1e-12);
    if (result.quantity_filled > 1e-12) {
        result.avg_fill_price  = limit_price;
        result.total_notional  = result.quantity_filled * limit_price;
        result.commission_usd  = result.total_notional * maker_fee_rate;
    }
    return result;
}

OrderFillResult aggregate_fills(
    const std::vector<LegFillResult>& legs,
    double requested_qty)
{
    OrderFillResult out;
    double total_notional = 0.0;
    for (const auto& leg : legs) {
        out.legs.push_back(leg);
        out.total_quantity_filled += leg.quantity_filled;
        total_notional            += leg.total_notional;
        out.total_commission_usd  += leg.commission_usd;
    }
    if (out.total_quantity_filled > 1e-12)
        out.weighted_avg_price = total_notional / out.total_quantity_filled;
    out.fully_filled = (out.total_quantity_filled >= requested_qty - 1e-12);
    return out;
}
