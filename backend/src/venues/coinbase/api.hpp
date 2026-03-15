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

class CoinbaseVenueApi final : public IVenueApi {
public:
    std::string name() const override { return "Coinbase"; }

    std::vector<std::string> list_supported_pairs() const override {
        ensure_pairs_loaded();
        return supported_pairs_;
    }

    // Coinbase Advanced Trade fee endpoint requires authentication.
    // Use the publicly documented volume-tiered schedule as the starting point;
    // upgrade to the authenticated /fees endpoint when API keys are configured.
    // Source: https://help.coinbase.com/en/exchange/trading-and-funding/exchange-fees
    VenueInfo fetch_venue_info() const override {
        VenueInfo info;
        info.fees.fetched_from_api = false;

        // Coinbase Advanced Trade tiered fee schedule (as of 2025).
        // {volume_threshold_usd, maker_fee, taker_fee}
        info.fees.tiers = {
            {           0.0, 0.0040, 0.0060},   //        $0 –   $10K
            {       10000.0, 0.0025, 0.0040},   //      $10K –   $50K
            {       50000.0, 0.0015, 0.0025},   //      $50K –  $100K
            {      100000.0, 0.0010, 0.0020},   //     $100K –    $1M
            {     1000000.0, 0.0008, 0.0018},   //       $1M –   $15M
            {    15000000.0, 0.0005, 0.0015},   //      $15M –   $75M
            {    75000000.0, 0.0000, 0.0010},   //      $75M –  $250M
            {   250000000.0, 0.0000, 0.0008},   //     $250M –  $400M
            {   400000000.0, 0.0000, 0.0005},   //     $400M+
        };
        std::cerr << "[coinbase] Using documented fee schedule ("
                  << info.fees.tiers.size()
                  << " tiers); authenticated fee endpoint not configured."
                  << std::endl;
        return info;
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
