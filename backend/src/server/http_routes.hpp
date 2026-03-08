#pragma once

#include <boost/beast/http.hpp>
#include <string>

class FeedManager;

void handle_request(
    FeedManager& feeds,
    const std::string& db_conn_str,
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    boost::beast::http::response<boost::beast::http::string_body>& res);
