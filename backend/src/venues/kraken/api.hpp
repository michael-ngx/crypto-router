#pragma once

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <simdjson.h>

#include "md/symbol_codec.hpp"
#include "venues/http_json.hpp"
#include "venues/venue_api.hpp"

class KrakenVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Kraken"; }

    std::vector<std::string> list_supported_pairs() const override {
        ensure_pairs_loaded();
        return supported_pairs_;
    }

    // Fetch fee schedule from Kraken's public AssetPairs endpoint.
    // The response includes volume-tiered fee arrays which we parse
    // into a full fee ladder (taker from "fees", maker from "fees_maker").
    VenueInfo fetch_venue_info() const override {
        VenueInfo info;

        // Query a single well-known pair to minimise payload.
        auto body = venues::http_json::https_get(
            "api.kraken.com",
            "/0/public/AssetPairs?pair=XBTUSD",
            "crypto-router/0.1"
        );
        if (!body) {
            std::cerr << "[kraken] Could not fetch fee schedule; using fallback." << std::endl;
            info.fees = fallback_fee_schedule();
            return info;
        }

        try {
            simdjson::padded_string json(*body);
            simdjson::ondemand::parser parser;
            auto doc = parser.iterate(json);

            simdjson::ondemand::object result_obj;

            const auto ec = doc["result"].get_object().get(result_obj);
            if (ec) {
                std::cerr << "[kraken] AssetPairs: invalid/missing 'result' object ("
                        << simdjson::error_message(ec)
                        << "); using fallback fee schedule."
                        << std::endl;
                info.fees = fallback_fee_schedule();
                return info;
            }

            for (auto pair_kv : result_obj) {
                simdjson::ondemand::value pair_val = pair_kv.value();

                // "fees": [[volume_threshold, percent_fee], ...] — taker schedule
                auto taker_tiers = parse_fee_ladder(pair_val, "fees");
                // "fees_maker": [[volume_threshold, percent_fee], ...]
                auto maker_tiers = parse_fee_ladder(pair_val, "fees_maker");

                if (taker_tiers.empty() || maker_tiers.empty()) {
                    break;  // parsing failed, will fall through to fallback
                }

                // Merge the two ladders into a unified tier vector.
                // Both arrays share the same set of volume thresholds on Kraken.
                info.fees.tiers.clear();
                info.fees.tiers.reserve(taker_tiers.size());
                for (std::size_t i = 0; i < taker_tiers.size(); ++i) {
                    FeeTier tier;
                    tier.volume_threshold = taker_tiers[i].first;
                    tier.taker_fee = taker_tiers[i].second;
                    tier.maker_fee = (i < maker_tiers.size())
                        ? maker_tiers[i].second
                        : taker_tiers[i].second;
                    info.fees.tiers.push_back(tier);
                }
                info.fees.fetched_from_api = true;
                break;  // one pair is enough for venue-level fees
            }
        } catch (const std::exception&) {
            // keep whatever we have
        }

        if (info.fees.tiers.empty()) {
            info.fees = fallback_fee_schedule();
        }

        log_fees(info);
        return info;
    }

private:
    // Parse a full fee ladder from a Kraken fee-tier array field.
    // Array format: [[volume_threshold, percent_fee], ...]
    // Returns pairs of (volume_threshold_usd, fee_as_fraction).
    static std::vector<std::pair<double, double>> parse_fee_ladder(
        simdjson::ondemand::value& pair_val,
        std::string_view field_name)
    {
        std::vector<std::pair<double, double>> ladder;
        simdjson::ondemand::array arr;
        if (pair_val[field_name].get_array().get(arr)) {
            return ladder;
        }
        for (auto tier : arr) {
            simdjson::ondemand::array tier_arr;
            if (tier.get_array().get(tier_arr)) break;
            double volume = 0.0;
            double pct = 0.0;
            std::size_t elem_idx = 0;
            for (auto elem : tier_arr) {
                if (elem_idx == 0) {
                    // volume threshold — Kraken sends as int
                    std::int64_t vol_int = 0;
                    if (!elem.get_int64().get(vol_int)) {
                        volume = static_cast<double>(vol_int);
                    } else {
                        (void)elem.get_double().get(volume);
                    }
                } else if (elem_idx == 1) {
                    (void)elem.get_double().get(pct);
                }
                ++elem_idx;
            }
            ladder.emplace_back(volume, pct / 100.0);  // percent -> fraction
        }
        return ladder;
    }

    // Documented Kraken base-tier fees as a fallback single-tier ladder.
    static VenueFeeSchedule fallback_fee_schedule() {
        VenueFeeSchedule schedule;
        schedule.fetched_from_api = false;
        schedule.tiers.push_back(FeeTier{0.0, 0.0025, 0.0040});  // 25/40 bps
        return schedule;
    }

    static void log_fees(const VenueInfo& info) {
        const auto& sched = info.fees;
        std::cout << "[kraken] Fee schedule"
                  << (sched.fetched_from_api ? " (from API)" : " (fallback)")
                  << ": " << sched.tiers.size() << " tier(s)";
        if (!sched.tiers.empty()) {
            const auto& base = sched.tiers.front();
            std::cout << ", base maker=" << (base.maker_fee * 10000) << "bps"
                      << " taker=" << (base.taker_fee * 10000) << "bps";
            if (sched.tiers.size() > 1) {
                const auto& top = sched.tiers.back();
                std::cout << ", top maker=" << (top.maker_fee * 10000) << "bps"
                          << " taker=" << (top.taker_fee * 10000) << "bps"
                          << " (>=$" << top.volume_threshold << ")";
            }
        }
        std::cout << std::endl;
    }

    void ensure_pairs_loaded() const {
        std::call_once(init_once_, [this]() {
            auto body = venues::http_json::https_get(
                "api.kraken.com",
                "/0/public/AssetPairs",
                "crypto-router/0.1"
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

                simdjson::ondemand::object result_obj;
                if (doc_res["result"].get_object().get(result_obj)) {
                    return;
                }

                std::unordered_set<std::string> seen;
                for (auto pair_kv : result_obj) {
                    simdjson::ondemand::value pair_val = pair_kv.value();
                    std::string_view wsname_sv;
                    if (pair_val["wsname"].get_string().get(wsname_sv)) {
                        continue;
                    }

                    std::string canonical =
                        SymbolCodec::to_canonical("Kraken", std::string(wsname_sv));
                    if (!SymbolCodec::is_canonical_pair(canonical)) {
                        continue;
                    }
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
