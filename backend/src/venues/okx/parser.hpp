#pragma once
#include "md/book_parser.hpp"
#include "md/book_events.hpp"
#include "md/symbol_codec.hpp"

#include <simdjson.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Parses OKX Spot Order Book channel (books or books5).
// Message format: {"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot"|"update","data":[{asks,bids,...}]}
// Each level: [price, size, deprecated, num_orders] - we use price (0) and size (1). Size "0" = delete.
// OKX instId format matches canonical (e.g. BTC-USDT).
// Uses simdjson DOM to avoid ondemand iterator invalidation when reading bids then asks.
class OkxBookParser : public IBookParser {
public:
    OkxBookParser() = default;

    bool parse(const std::string& raw, std::vector<BookEvent>& out) override {
        if (raw.find("\"channel\":\"books") == std::string::npos &&
            raw.find("\"channel\":\"books5") == std::string::npos)
            return false;
        if (raw.find("\"data\"") == std::string::npos) return false;

        simdjson::padded_string pj(raw);
        simdjson::dom::element doc;
        if (parser_.parse(pj).get(doc)) return false;

        // Skip non-data messages (subscribe ack has "event", not "data" as array of book data)
        simdjson::dom::object arg_obj;
        if (doc["arg"].get_object().get(arg_obj)) return false;

        std::string_view inst_sv;
        if (arg_obj["instId"].get_string().get(inst_sv)) return false;
        const std::string canonical = SymbolCodec::to_canonical("okx", std::string(inst_sv));

        simdjson::dom::array data_arr;
        if (doc["data"].get_array().get(data_arr)) return false;

        std::string_view channel_sv;
        const bool has_channel = !arg_obj["channel"].get_string().get(channel_sv);
        const bool is_books5 = has_channel && (channel_sv == "books5");
        std::string_view action_sv;
        const bool has_action = !doc["action"].get_string().get(action_sv);
        // Treat as snapshot unless explicitly action=="update" (incremental)
        const bool is_snapshot = is_books5 || !(has_action && action_sv == "update");

        const auto now_ns = monotonic_ns();
        bool produced = false;

        for (simdjson::dom::element data_elem : data_arr) {
            simdjson::dom::object data_obj;
            if (data_elem.get_object().get(data_obj)) continue;

            if (is_snapshot) {
                BookEventSnapshot snap;
                snap.venue  = "OKX";
                snap.symbol = canonical;
                snap.ts_ns  = now_ns;

                emit_side(data_obj, "bids", canonical, BookSide::Bid, now_ns, snap.levels);
                emit_side(data_obj, "asks", canonical, BookSide::Ask, now_ns, snap.levels);

                if (!snap.levels.empty()) {
                    out.emplace_back(std::move(snap));
                    produced = true;
                }
            } else {
                emit_updates(data_obj, "bids", canonical, BookSide::Bid, now_ns, out, produced);
                emit_updates(data_obj, "asks", canonical, BookSide::Ask, now_ns, out, produced);
            }
        }
        return produced;
    }

private:
    static std::int64_t monotonic_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static double parse_price_or_size(simdjson::dom::element e) {
        switch (e.type()) {
            case simdjson::dom::element_type::STRING: {
                std::string_view sv;
                if (e.get_string().get(sv)) return 0.0;
                std::string s(sv);
                return std::strtod(s.c_str(), nullptr);
            }
            case simdjson::dom::element_type::DOUBLE:
            case simdjson::dom::element_type::INT64:
            case simdjson::dom::element_type::UINT64: {
                double v;
                if (e.get_double().get(v)) return 0.0;
                return v;
            }
            default:
                return 0.0;
        }
    }

    static void emit_side(simdjson::dom::object& obj,
                          const char* key,
                          const std::string& canonical,
                          BookSide side,
                          std::int64_t ts_ns,
                          std::vector<BookEventDelta>& levels) {
        simdjson::dom::array arr;
        if (obj[key].get_array().get(arr)) return;

        for (simdjson::dom::element elem : arr) {
            simdjson::dom::array level_arr;
            if (elem.get_array().get(level_arr)) continue;

            double px = 0.0, sz = 0.0;
            std::size_t idx = 0;
            for (simdjson::dom::element e : level_arr) {
                if (idx == 0) px = parse_price_or_size(e);
                else if (idx == 1) sz = parse_price_or_size(e);
                ++idx;
            }
            if (px <= 0.0) continue;

            BookEventDelta d;
            d.venue  = "OKX";
            d.symbol = canonical;
            d.side   = side;
            d.price  = px;
            d.size   = sz;
            d.op     = (sz == 0.0) ? BookOp::Delete : BookOp::Upsert;
            d.ts_ns  = ts_ns;
            levels.emplace_back(std::move(d));
        }
    }

    static void emit_updates(simdjson::dom::object& obj,
                             const char* key,
                             const std::string& canonical,
                             BookSide side,
                             std::int64_t ts_ns,
                             std::vector<BookEvent>& out,
                             bool& produced_flag) {
        simdjson::dom::array arr;
        if (obj[key].get_array().get(arr)) return;

        for (simdjson::dom::element elem : arr) {
            simdjson::dom::array level_arr;
            if (elem.get_array().get(level_arr)) continue;

            double px = 0.0, sz = 0.0;
            std::size_t idx = 0;
            for (simdjson::dom::element e : level_arr) {
                if (idx == 0) px = parse_price_or_size(e);
                else if (idx == 1) sz = parse_price_or_size(e);
                ++idx;
            }
            if (px <= 0.0) continue;

            BookEventDelta d;
            d.venue  = "OKX";
            d.symbol = canonical;
            d.side   = side;
            d.price  = px;
            d.size   = sz;
            d.op     = (sz == 0.0) ? BookOp::Delete : BookOp::Upsert;
            d.ts_ns  = ts_ns;
            out.emplace_back(std::move(d));
            produced_flag = true;
        }
    }

    simdjson::dom::parser parser_;
};
