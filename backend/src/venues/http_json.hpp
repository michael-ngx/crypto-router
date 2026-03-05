#pragma once

#include <iostream>
#include <optional>
#include <string>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>

namespace venues::http_json {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

inline std::optional<std::string> https_get(
    const std::string& host,
    const std::string& target,
    const std::string& user_agent)
{
    try {
        net::io_context ioc;
        net::ssl::context ssl_ctx{net::ssl::context::tls_client};
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(net::ssl::verify_peer);

        tcp::resolver resolver{ioc};
        beast::ssl_stream<beast::tcp_stream> stream{ioc, ssl_ctx};

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            beast::error_code ec(
                static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()
            );
            throw beast::system_error(ec);
        }

        auto const results = resolver.resolve(host, "443");
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(net::ssl::stream_base::client);

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, user_agent);
        req.set(http::field::accept, "application/json");
        req.set(http::field::connection, "close");
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == net::ssl::error::stream_truncated || ec == net::error::eof) {
            ec = {};
        }
        if (ec) {
            std::cerr << "[venue-api] TLS shutdown error for " << host
                      << target << ": " << ec.message() << std::endl;
        }

        if (res.result_int() < 200 || res.result_int() >= 300) {
            std::cerr << "[venue-api] HTTP " << res.result_int()
                      << " when fetching " << host << target << std::endl;
            return std::nullopt;
        }
        return res.body();
    } catch (const std::exception& e) {
        std::cerr << "[venue-api] Failed to fetch " << host << target
                  << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

} // namespace venues::http_json
