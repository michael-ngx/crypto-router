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
    // url.path() => "/api/book"
    // url.params() => iterable view of query parameters

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
            try {
                std::size_t d = std::stoul(std::string(p.value));
                if (d > 0 && d <= MAX_TOP_DEPTH) {
                    depth = d;
                }
            } catch (...) {
                // ignore invalid input
            }
        }

        UIConsolidated snap = ui.snapshot_consolidated(depth);

        std::ostringstream os;
        os << "{";
        os << "\"symbol\":\"" << json_escape(snap.symbol) << "\",";
        os << "\"bids\":"; json_pair_array(os, snap.bids); os << ",";
        os << "\"asks\":"; json_pair_array(os, snap.asks); os << ",";
        os << "\"per_venue\":{";
        bool first = true;
        for (const auto& kv : snap.per_venue) {
            if (!kv.second) continue;
            if (!first) os << ",";
            first = false;
            os << "\"" << json_escape(kv.first) << "\":{";
            os << "\"venue\":\"" << json_escape(kv.second->venue) << "\",";
            os << "\"symbol\":\"" << json_escape(kv.second->symbol) << "\",";
            os << "\"ts_ns\":" << kv.second->ts_ns << ",";
            os << "\"bids\":"; json_pair_array(os, kv.second->bids); os << ",";
            os << "\"asks\":"; json_pair_array(os, kv.second->asks);
            os << "}";
        }
        os << "}";
        os << "}";

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