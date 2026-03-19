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

// Parses Binance Spot Partial Book Depth stream (depth20@100ms).
// Each message is a full snapshot of top 20 levels.
// Stream format: wss://stream.binance.com:9443/ws/<symbol>@depth20@100ms
// Message format: {"lastUpdateId":N,"bids":[["price","qty"]],"asks":[["price","qty"]]}
// Symbol is known from the subscription; we receive per-symbol stream so symbol is
// not in the message. We use the combined stream to get symbol in the wrapper:
// wss://stream.binance.com:9443/stream?streams=<symbol>@depth20@100ms
// Wrapper: {"stream":"btcusdt@depth20@100ms","data":{"lastUpdateId":...,"bids":...,"asks":...}}
class BinanceBookParser : public IBookParser {
public:
    BinanceBookParser() = default;

    bool parse(const std::string& raw, std::vector<BookEvent>& out) override {
        // Accept either raw depth message (single stream) or wrapped (combined stream)
        const bool has_stream = raw.find("\"stream\"") != std::string::npos;
        const bool has_depth = raw.find("\"bids\"") != std::string::npos &&
                              raw.find("\"asks\"") != std::string::npos;
        if (!has_depth) return false;

        simdjson::padded_string pj(raw);
        auto doc_res = parser_.iterate(pj);
        if (auto err = doc_res.error()) {
            std::cerr << "[binance-parser] iterate error: " << err << "\n";
            return false;
        }
        simdjson::ondemand::document doc = std::move(doc_res.value());

        simdjson::ondemand::object data_obj;
        std::string canonical;

        if (has_stream) {
            std::string_view stream_sv;
            if (doc["stream"].get_string().get(stream_sv)) return false;
            // "btcusdt@depth20@100ms" -> "btcusdt"
            std::string stream_str(stream_sv);
            const std::size_t at = stream_str.find('@');
            const std::string venue_sym = (at != std::string::npos)
                ? stream_str.substr(0, at) : stream_str;
            canonical = SymbolCodec::to_canonical("binance", venue_sym);

            if (doc["data"].get_object().get(data_obj)) return false;
        } else {
            // Single stream - we don't have symbol in message. Cannot parse.
            // Combined stream is required for symbol extraction.
            return false;
        }

        const auto now_ns = monotonic_ns();

        BookEventSnapshot snap;
        snap.venue  = "Binance";
        snap.symbol = canonical;
        snap.ts_ns  = now_ns;

        emit_side(data_obj, "bids", canonical, BookSide::Bid, now_ns, snap.levels);
        emit_side(data_obj, "asks", canonical, BookSide::Ask, now_ns, snap.levels);

        if (!snap.levels.empty()) {
            out.emplace_back(std::move(snap));
            return true;
        }
        return false;
    }

private:
    static std::int64_t monotonic_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static double fast_atof(std::string_view sv) {
        constexpr std::size_t kBufSize = 32;
        if (sv.empty() || sv.size() >= kBufSize) return 0.0;
        char buf[kBufSize];
        sv.copy(buf, sv.size());
        buf[sv.size()] = '\0';
        return std::strtod(buf, nullptr);
    }

    static void emit_side(simdjson::ondemand::object& obj,
                          const char* key,
                          const std::string& canonical,
                          BookSide side,
                          std::int64_t ts_ns,
                          std::vector<BookEventDelta>& levels) {
        auto arr_res = obj[key].get_array();
        if (arr_res.error()) return;
        auto arr = arr_res.value();

        for (auto elem : arr) {
            simdjson::ondemand::array level_arr;
            if (elem.get_array().get(level_arr)) continue;

            std::size_t idx = 0;
            std::string_view px_sv, qty_sv;
            for (auto e : level_arr) {
                std::string_view s;
                if (e.get_string().get(s)) continue;
                if (idx == 0) px_sv = s;
                else if (idx == 1) qty_sv = s;
                ++idx;
            }
            if (idx < 2) continue;

            const double px = fast_atof(px_sv);
            const double qty = fast_atof(qty_sv);
            if (px <= 0.0 || qty <= 0.0) continue;

            BookEventDelta d;
            d.venue  = "Binance";
            d.symbol = canonical;
            d.side   = side;
            d.price  = px;
            d.size   = qty;
            d.op     = BookOp::Upsert;
            d.ts_ns  = ts_ns;
            levels.emplace_back(std::move(d));
        }
    }

    simdjson::ondemand::parser parser_;
};
