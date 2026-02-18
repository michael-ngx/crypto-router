#pragma once
#include "md/book_parser.hpp"
#include "md/book_events.hpp"
#include "md/symbol_codec.hpp"

#include <simdjson.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <iostream>

class CoinbaseBookParser : public IBookParser {
public:
    CoinbaseBookParser() = default;

    bool parse(const std::string& raw, std::vector<BookEvent>& out) override {
        if (raw.find("\"channel\":\"l2_data\"") == std::string::npos) return false;

        // Make a safely padded copy for simdjson ondemand
        simdjson::padded_string pj(raw);
        auto doc_res = parser_.iterate(pj);
        if (auto err = doc_res.error()) {
            std::cerr << "[coinbase-parser] iterate error: " << err << "\n";
            return false;
        }
        simdjson::ondemand::document doc = std::move(doc_res.value());

        // events[]
        auto events_res = doc["events"].get_array();
        if (auto err = events_res.error()) {
            std::cerr << "[coinbase-parser] events get_array error: " << err << "\n";
            return false;
        }
        auto events = events_res.value();

        const auto now_ns = monotonic_ns();

        for (simdjson::ondemand::value ev_val : events) {
            simdjson::ondemand::object ev;
            if (auto err = ev_val.get_object().get(ev)) {
                std::cerr << "[coinbase-parser] event get_object error: " << err << "\n";
                continue;
            }

            std::string_view type_sv, prod_sv;
            if (ev["type"].get(type_sv)) continue;
            if (ev["product_id"].get(prod_sv)) continue;
            const std::string canonical = SymbolCodec::to_canonical("coinbase", std::string(prod_sv));

            simdjson::ondemand::array updates;
            if (ev["updates"].get(updates)) continue;

            if (type_sv == "snapshot") {
                BookSnapshot snap;
                snap.venue  = "Coinbase";
                snap.symbol = canonical;
                snap.ts_ns  = now_ns;

                for (auto u : updates) {
                    simdjson::ondemand::object o;
                    if (u.get_object().get(o)) continue;

                    std::string_view side_sv, px_sv, qty_sv;
                    if (o["side"].get(side_sv)) continue;
                    if (o["price_level"].get(px_sv)) continue;   // usually strings
                    if (o["new_quantity"].get(qty_sv)) continue; // usually strings

                    BookDelta d;
                    d.venue  = "Coinbase";
                    d.symbol = snap.symbol;
                    d.side   = (side_sv == "bid") ? BookSide::Bid : BookSide::Ask;
                    d.price  = fast_atof(px_sv);
                    d.size   = fast_atof(qty_sv);
                    d.op     = (d.size == 0.0) ? BookOp::Delete : BookOp::Upsert;
                    d.ts_ns  = now_ns;
                    snap.levels.emplace_back(std::move(d));
                }
                if (!snap.levels.empty()) out.emplace_back(std::move(snap));
            } else if (type_sv == "update") {
                for (auto u : updates) {
                    simdjson::ondemand::object o;
                    if (u.get_object().get(o)) continue;

                    std::string_view side_sv, px_sv, qty_sv;
                    if (o["side"].get(side_sv)) continue;
                    if (o["price_level"].get(px_sv)) continue;
                    if (o["new_quantity"].get(qty_sv)) continue;

                    BookDelta d;
                    d.venue  = "Coinbase";
                    d.symbol = canonical;
                    d.side   = (side_sv == "bid") ? BookSide::Bid : BookSide::Ask;
                    d.price  = fast_atof(px_sv);
                    d.size   = fast_atof(qty_sv);
                    d.op     = (d.size == 0.0) ? BookOp::Delete : BookOp::Upsert;
                    d.ts_ns  = now_ns;
                    out.emplace_back(std::move(d));
                }
            }
        }
        return !out.empty();
    }

private:
    static std::int64_t monotonic_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
    static double fast_atof(std::string_view sv) {
        // robust and fast enough; avoids locale pitfalls
        char* end = nullptr;
        std::string tmp(sv); // small strings SSO, big strings still cheap vs parsing
        double v = std::strtod(tmp.c_str(), &end);
        return (end == tmp.c_str()) ? 0.0 : v;
    }

    simdjson::ondemand::parser parser_;
};
