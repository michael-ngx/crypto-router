#pragma once
#include "md/book_parser.hpp"
#include "md/book_events.hpp"
#include "md/symbol_codec.hpp"

#include <simdjson.h>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>

class KrakenBookParser : public IBookParser {
public:
    KrakenBookParser() = default;

    bool parse(const std::string& raw, std::vector<BookEvent>& out) override {
        // Fast reject for irrelevant messages
        if (raw.find("\"channel\":\"book\"") == std::string::npos ||
            raw.find("\"method\":\"subscribe\"") != std::string::npos)
            return false;

        simdjson::padded_string pj(raw);
        auto doc_res = parser_.iterate(pj);
        if (auto err = doc_res.error()) {
            std::cerr << "[kraken-parser] iterate error: " << err << "\n";
            return false;
        }
        simdjson::ondemand::document doc = std::move(doc_res.value());

        // Read message type
        std::string_view type_sv;
        if (auto err = doc["type"].get(type_sv)) {
            std::cerr << "[kraken-parser] missing type: " << err << "\n";
            return false;
        }

        // Extract data array
        auto data_arr_res = doc["data"].get_array();
        if (auto err = data_arr_res.error()) {
            std::cerr << "[kraken-parser] data get_array error: " << err << "\n";
            return false;
        }
        auto data_arr = data_arr_res.value();

        const auto now_ns = monotonic_ns();
        bool produced = false;

        for (simdjson::ondemand::value dv : data_arr) {
            simdjson::ondemand::object obj;
            if (dv.get_object().get(obj)) continue;

            std::string_view sym_sv;
            if (obj["symbol"].get(sym_sv)) continue;

            const std::string canonical = SymbolCodec::to_canonical("kraken", std::string(sym_sv));

            if (type_sv == "snapshot") {
                BookSnapshot snap;
                snap.venue  = "Kraken";
                snap.symbol = canonical;
                snap.ts_ns  = now_ns;

                // parse bids
                emit_side(obj, "bids", canonical, BookSide::Bid, now_ns, snap.levels);
                // parse asks
                emit_side(obj, "asks", canonical, BookSide::Ask, now_ns, snap.levels);

                if (!snap.levels.empty()) {
                    out.emplace_back(std::move(snap));
                    produced = true;
                }
            } else if (type_sv == "update") {
                emit_updates(obj, "bids", canonical, BookSide::Bid, now_ns, out, produced);
                emit_updates(obj, "asks", canonical, BookSide::Ask, now_ns, out, produced);
            }
        }
        return produced;
    }

private:
    static std::int64_t monotonic_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static void emit_side(simdjson::ondemand::object& obj,
                          const char* key,
                          const std::string& canonical,
                          BookSide side,
                          std::int64_t ts_ns,
                          std::vector<BookDelta>& levels) {
        auto arr_res = obj[key].get_array();
        if (arr_res.error()) return;
        auto arr = arr_res.value();

        for (auto elem : arr) {
            simdjson::ondemand::object level;
            if (elem.get_object().get(level)) continue;

            double px = 0.0, qty = 0.0;
            if (level["price"].get(px)) continue;
            if (level["qty"].get(qty)) continue;

            BookDelta d;
            d.venue  = "Kraken";
            d.symbol = canonical;
            d.side   = side;
            d.price  = px;
            d.size   = qty;
            d.op     = (qty == 0.0) ? BookOp::Delete : BookOp::Upsert;
            d.ts_ns  = ts_ns;
            levels.emplace_back(std::move(d));
        }
    }

    static void emit_updates(simdjson::ondemand::object& obj,
                             const char* key,
                             const std::string& canonical,
                             BookSide side,
                             std::int64_t ts_ns,
                             std::vector<BookEvent>& out,
                             bool& produced_flag) {
        auto arr_res = obj[key].get_array();
        if (arr_res.error()) return;
        auto arr = arr_res.value();

        for (auto elem : arr) {
            simdjson::ondemand::object level;
            if (elem.get_object().get(level)) continue;

            double px = 0.0, qty = 0.0;
            if (level["price"].get(px)) continue;
            if (level["qty"].get(qty)) continue;

            BookDelta d;
            d.venue  = "Kraken";
            d.symbol = canonical;
            d.side   = side;
            d.price  = px;
            d.size   = qty;
            d.op     = (qty == 0.0) ? BookOp::Delete : BookOp::Upsert;
            d.ts_ns  = ts_ns;
            out.emplace_back(std::move(d));
            produced_flag = true;
        }
    }

    simdjson::ondemand::parser parser_;
};
