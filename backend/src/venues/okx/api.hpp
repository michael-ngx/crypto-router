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

class OkxVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "OKX"; }

    std::vector<std::string> list_supported_pairs() const override {
        ensure_pairs_loaded();
        return supported_pairs_;
    }

    VenueStaticInfo fetch_venue_static_info() const override {
        VenueStaticInfo info;
        info.fees.fetched_from_api = false;

        // OKX Spot base fee schedule (as of 2025).
        // Source: https://www.okx.com/en-gb/fees
        info.fees.tiers = {
            {0.0, 0.0008, 0.0010},   // 0.08% maker, 0.10% taker
        };
        std::cerr << "[okx] Using documented base fee schedule (0.08%/0.10%); "
                  << "volume/OKB tiers require authentication."
                  << std::endl;
        return info;
    }

private:
    void ensure_pairs_loaded() const {
        std::call_once(init_once_, [this]() {
            auto body = venues::http_json::https_get(
                "www.okx.com",
                "/api/v5/public/instruments?instType=SPOT",
                "crypto-router/0.1",
                16 * 1024 * 1024  // 16MB body limit
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

                simdjson::ondemand::array data_arr;
                if (doc_res["data"].get_array().get(data_arr)) {
                    return;
                }

                std::unordered_set<std::string> seen;
                for (auto inst : data_arr) {
                    std::string_view state_sv;
                    if (inst["state"].get_string().get(state_sv)) continue;
                    if (state_sv != "live") continue;

                    std::string_view inst_id_sv;
                    if (inst["instId"].get_string().get(inst_id_sv)) continue;

                    const std::string canonical =
                        SymbolCodec::to_canonical("okx", std::string(inst_id_sv));

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
