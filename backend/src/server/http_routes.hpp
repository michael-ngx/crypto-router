#pragma once
#include <boost/beast/http.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <cstdlib>
#include "util/json_encode.hpp"
#include "pipeline/master_feed.hpp"

namespace http = boost::beast::http;

inline void handle_request(UIMasterFeed& ui,
                           const http::request<http::string_body>& req,
                           http::response<http::string_body>& res)
{
    res.set(http::field::server, "md-router/0.1");

    // /api/health
    if (req.method() == http::verb::get && req.target() == "/api/health") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"status":"ok"})";
        return;
    }

    // /api/book?depth=10
    std::string target = std::string(req.target());
    if (target.rfind("/api/book", 0) == 0) {
        std::size_t depth  = 10;
        auto qpos = target.find('?');
        if (qpos != std::string::npos) {
            auto qs = target.substr(qpos+1);
            std::size_t pos=0;
            while (pos < qs.size()) {
                auto amp = qs.find('&', pos);
                std::string kv = qs.substr(pos, amp==std::string::npos ? std::string::npos : amp-pos);
                auto eq = kv.find('=');
                if (eq != std::string::npos) {
                    auto key = kv.substr(0, eq);
                    auto val = kv.substr(eq+1);
                    if (key == "depth") {
                        char* end=nullptr;
                        long d = std::strtol(val.c_str(), &end, 10);
                        if (end && *end=='\0' && d>0 && d<10000) depth = static_cast<std::size_t>(d);
                    }
                }
                if (amp == std::string::npos) break;
                pos = amp+1;
            }
        }

        UIConsolidated snap = ui.snapshot_consolidated(depth);

        std::ostringstream os;
        os << "{";
        os << "\"symbol\":\"" << json_escape(snap.symbol) << "\",";
        os << "\"bids\":"; json_pair_array(os, snap.bids); os << ",";
        os << "\"asks\":"; json_pair_array(os, snap.asks); os << ",";
        os << "\"per_venue\":{";
        bool first=true;
        for (const auto& kv : snap.per_venue) {
            if (!kv.second) continue;
            if (!first) os << ",";
            first=false;
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