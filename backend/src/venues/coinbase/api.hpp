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

class CoinbaseVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Coinbase"; }

    std::vector<std::string> list_supported_pairs() const override {
        ensure_pairs_loaded();
        return supported_pairs_;
    }

private:
    void ensure_pairs_loaded() const {
        std::call_once(init_once_, [this]() {
            auto body = venues::http_json::https_get(
                "api.exchange.coinbase.com",
                "/products",
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

                simdjson::ondemand::array products;
                if (doc_res.get_array().get(products)) {
                    return;
                }

                std::unordered_set<std::string> seen;
                for (auto product : products) {
                    std::string_view id_sv;
                    if (product["id"].get_string().get(id_sv)) {
                        continue;
                    }

                    bool trading_disabled = false;
                    auto disabled_val = product["trading_disabled"];
                    if (!disabled_val.error()) {
                        bool tmp = false;
                        if (!disabled_val.get_bool().get(tmp)) {
                            trading_disabled = tmp;
                        }
                    }
                    if (trading_disabled) {
                        continue;
                    }

                    std::string pair(id_sv);
                    if (!SymbolCodec::is_canonical_pair(pair)) {
                        continue;
                    }
                    if (seen.insert(pair).second) {
                        supported_pairs_.push_back(pair);
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
