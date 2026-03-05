#pragma once

#include <algorithm>
#include <mutex>
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

private:
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
