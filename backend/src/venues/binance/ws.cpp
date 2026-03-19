#include "ws.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <openssl/ssl.h>
#include <atomic>
#include <memory>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct BinanceWs::Impl
{
    std::string host = "stream.binance.com";
    std::string symbol;
    OnMsg on_msg;

    net::io_context ioc{1};
    net::ssl::context ssl_ctx{net::ssl::context::tls_client};
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws;
    std::atomic<bool> stop_flag{false};

    Impl(std::string sym, OnMsg cb)
        : symbol(std::move(sym)), on_msg(std::move(cb))
    {
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(net::ssl::verify_peer);
    }

    void run(unsigned short port)
    {
        try
        {
            // Combined stream: /stream?streams=<symbol>@depth20@100ms
            // Parser expects wrapped format {"stream":"...","data":{...}}
            std::string path = "/stream?streams=" + symbol + "@depth20@100ms";

            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host, std::to_string(port));

            ws = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(ioc, ssl_ctx);

            net::connect(beast::get_lowest_layer(*ws), results);

            if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
                throw beast::system_error{
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "SNI set failed"
                };
            }

            ws->next_layer().handshake(net::ssl::stream_base::client);

            ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "binance-ws-connector/0.1");
            }));
            ws->handshake(host, path);

            beast::flat_buffer buffer;
            while (!stop_flag.load(std::memory_order_relaxed))
            {
                buffer.clear();
                beast::error_code ec;
                ws->read(buffer, ec);
                if (ec)
                {
                    if (ec == websocket::error::closed ||
                        ec == net::error::operation_aborted ||
                        ec == net::error::eof ||
                        ec == net::error::not_connected ||
                        ec == beast::errc::not_connected) {
                        break;
                    }
                    throw beast::system_error{ec};
                }
                std::string data = beast::buffers_to_string(buffer.cdata());
                if (on_msg) on_msg(data);
            }

            if (ws) {
                beast::error_code close_ec;
                ws->close(websocket::close_code::normal, close_ec);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "[binance-ws] error: " << e.what() << "\n";
        }
    }

    void stop() noexcept
    {
        stop_flag.store(true, std::memory_order_relaxed);
        if (ws) {
            net::post(ioc, [s = ws.get()] {
                beast::error_code ec;
                s->close(websocket::close_code::normal, ec);
                beast::get_lowest_layer(*s).shutdown(tcp::socket::shutdown_both, ec);
                beast::get_lowest_layer(*s).close(ec);
            });
        }
    }
};

BinanceWs::BinanceWs(std::string symbol, OnMsg cb)
    : impl_(new Impl(std::move(symbol), std::move(cb))) {}

BinanceWs::~BinanceWs() { delete impl_; }

void BinanceWs::start(unsigned short port) { impl_->run(port); }
void BinanceWs::stop() noexcept { impl_->stop(); }
