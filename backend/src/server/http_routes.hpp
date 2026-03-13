#pragma once

#include <boost/beast/http.hpp>
#include <cstdint>
#include <string>

class FeedManager;
namespace router { enum class RouterVersionId : std::uint8_t; }

void handle_request(
    FeedManager& feeds,
    const std::string& db_conn_str,
    router::RouterVersionId router_version,
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    boost::beast::http::response<boost::beast::http::string_body>& res);
