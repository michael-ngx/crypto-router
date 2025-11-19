#pragma once
#include <boost/url.hpp>
#include <boost/beast/http.hpp>
#include <string_view>
#include "util/json_encode.hpp"
#include "pipeline/master_feed.hpp"

namespace http  = boost::beast::http;
namespace urls  = boost::urls;

inline void handle_request(UIMasterFeed& ui,
                           const http::request<http::string_body>& req,
                           http::response<http::string_body>& res)
{
    res.set(http::field::server, "md-router/0.1");

    // Parse the target as an origin-form URL
    std::string_view target{req.target().data(), req.target().size()};
    auto parsed_result = urls::parse_origin_form(target);
    if (!parsed_result) {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"bad request"})";
        return;
    }

    urls::url_view url = *parsed_result;

    // /api/health
    if (req.method() == http::verb::get && url.path() == "/api/health") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"status":"ok"})";
        return;
    }

    // /api/book?depth=10
    if (req.method() == http::verb::get && url.path() == "/api/book") {
        std::size_t depth = MAX_TOP_DEPTH;

        for (auto const& p : url.params()) {
            if (p.key == "depth") {
                try {
                    std::size_t d = std::stoul(std::string(p.value));
                    if (d > 0 && d <= MAX_TOP_DEPTH) {
                        depth = d;
                    }
                } catch (...) {
                    // ignore invalid input
                }
            }
        }

        UIConsolidated snap = ui.snapshot_consolidated(depth);

        std::ostringstream os;
        os << "{";
        os << "\"symbol\":\"" << json_escape(snap.symbol) << "\",";

        // Consolidated ladders with venue information for UI
        os << "\"bids\":"; json_ladder_array(os, snap.bids); os << ",";
        os << "\"asks\":"; json_ladder_array(os, snap.asks); os << ",";

        // Per-venue snapshots (still useful for debugging / side panels)
        os << "\"per_venue\":{";
        bool first = true;
        for (const auto& kv : snap.per_venue) {
            const auto& venue_name = kv.first;
            const auto& sp = kv.second;
            if (!sp) continue;

            if (!first) os << ",";
            first = false;

            os << "\"" << json_escape(venue_name) << "\":{";
            os << "\"venue\":\""  << json_escape(sp->venue)  << "\",";
            os << "\"symbol\":\"" << json_escape(sp->symbol) << "\",";
            os << "\"ts_ns\":"    << sp->ts_ns << ",";
            os << "\"bids\":"; json_pair_array(os, sp->bids); os << ",";
            os << "\"asks\":"; json_pair_array(os, sp->asks);
            os << "}";
        }
        os << "}"; // per_venue
        os << "}"; // root object

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = os.str();
        return;
    }

    // 404
    res.result(http::status::not_found);
    res.set(http::field::content_type, "application/json");
    res.body() = R"({"error":"not found"})";
}