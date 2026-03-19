#pragma once

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <simdjson.h>

#include "md/symbol_codec.hpp"
#include "venues/http_json.hpp"
#include "venues/venue_api.hpp"

class BinanceVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Binance"; }

    std::vector<std::string> list_supported_pairs() const override {
        ensure_pairs_loaded();
        return supported_pairs_;
    }

    VenueStaticInfo fetch_venue_static_info() const override {
        VenueStaticInfo info;
        info.fees.fetched_from_api = false;

        // Binance Spot base fee schedule (as of 2025).
        // Volume-tiered VIP tiers require authentication.
        // Source: https://www.binance.com/en/fee/schedule
        info.fees.tiers = {
            {           0.0, 0.0010, 0.0010},   // Base: 0.1% maker/taker
        };
        std::cerr << "[binance] Using documented base fee schedule (0.1%); "
                  << "VIP tiers require API key."
                  << std::endl;
        return info;
    }

private:
    void ensure_pairs_loaded() const {
        std::call_once(init_once_, [this]() {
            // exchangeInfo returns all symbols with full metadata; can exceed 8MB default
            auto body = venues::http_json::https_get(
                "api.binance.com",
                "/api/v3/exchangeInfo",
                "crypto-router/0.1",
                32 * 1024 * 1024  // 32MB body limit
            );
            if (!body) {
                return;
            }

            try {
                simdjson::padded_string json(*body);
                simdjson::ondemand::parser parser;
                auto doc_res = parser.iterate(json);
                if (doc_res.error()) {
                    return;
                }

                simdjson::ondemand::array symbols_arr;
                if (doc_res["symbols"].get_array().get(symbols_arr)) {
                    return;
                }

                std::unordered_set<std::string> seen;
                for (auto sym : symbols_arr) {
                    std::string_view status_sv;
                    if (sym["status"].get_string().get(status_sv)) continue;
                    if (status_sv != "TRADING") continue;

                    std::string_view base_sv, quote_sv;
                    if (sym["baseAsset"].get_string().get(base_sv)) continue;
                    if (sym["quoteAsset"].get_string().get(quote_sv)) continue;

                    const std::string venue_sym =
                        std::string(base_sv) + std::string(quote_sv);
                    const std::string canonical =
                        SymbolCodec::to_canonical("binance", venue_sym);

                    if (!SymbolCodec::is_canonical_pair(canonical)) continue;
                    if (seen.insert(canonical).second) {
                        supported_pairs_.push_back(canonical);
                    }
                }

                std::sort(supported_pairs_.begin(), supported_pairs_.end());
            } catch (const std::exception&) {
                supported_pairs_.clear();
            }
        });
    }

    mutable std::once_flag init_once_;
    mutable std::vector<std::string> supported_pairs_;
};
