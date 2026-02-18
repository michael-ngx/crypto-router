#include "ws.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
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

struct CoinbaseWs::Impl
{
    std::string host = "advanced-trade-ws.coinbase.com";
    std::string channel = "level2";
    std::string product;
    OnMsg on_msg;

    net::io_context ioc{1};
    net::ssl::context ssl_ctx{net::ssl::context::tls_client};
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws;
    std::atomic<bool> stop_flag{false};

    Impl(std::string product_id, OnMsg cb)
    : product(std::move(product_id)), on_msg(std::move(cb))
    {
        // Recommended client settings
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(net::ssl::verify_peer);
    }

    void run(unsigned short port)
    {
        try
        {
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host, std::to_string(port));

            // Make the socket + SSL + WS stack
            ws = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(ioc, ssl_ctx);

            // TCP connect
            net::connect(beast::get_lowest_layer(*ws), results);

            // SNI (Server Name Indication) for TLS
            if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
                throw beast::system_error{
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "SNI set failed"
                };
            }

            // SSL handshake
            ws->next_layer().handshake(net::ssl::stream_base::client);

            // WS handshake
            ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            ws->set_option(websocket::stream_base::decorator([](websocket::request_type &req){
                req.set(http::field::user_agent, std::string("coinbase-ws-connector/0.3"));
            }));
            ws->handshake(host, "/");

            // Subscribe to Level 2 (order book) for the product
            // Payload shape per Coinbase Advanced Trade WS: channel "level2"
            // {"type":"subscribe","channel":"level2","product_ids":["BTC-USD"]}
            std::string sub = std::string("{\"type\":\"subscribe\",\"channel\":\"") + channel
                            + "\",\"product_ids\":[\"" + product + "\"]}";
            ws->write(net::buffer(sub)  );

            // Loop to read messages from websocket, calling on_msg callback for each message received
            beast::flat_buffer buffer;
            while (!stop_flag.load(std::memory_order_relaxed))
            {
                buffer.clear();
                beast::error_code ec;
                ws->read(buffer, ec);
                if (ec)
                {
                    // Expected during stop() or orderly remote shutdown
                    if (ec == websocket::error::closed ||
                        ec == net::error::operation_aborted ||
                        ec == net::error::eof ||
                        ec == net::error::not_connected ||
                        ec == beast::errc::not_connected) {
                        break;
                    }
                    // Expected during stop() or orderly remote shutdown
                    throw beast::system_error{ec};
                }
                std::string data = beast::buffers_to_string(buffer.cdata());
                if (on_msg) on_msg(data); // Callback with received message from websocket
            }

            // Clean shutdown
            if (ws) {
                beast::error_code close_ec;
                ws->close(websocket::close_code::normal, close_ec);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[coinbase-ws] error: " << e.what() << "\n";
        }
    }

    void stop() noexcept
    {
        stop_flag.store(true, std::memory_order_relaxed);
        if (ws) {
            net::post(ioc, [s = ws.get()] {
                beast::error_code ec;
                s->close(websocket::close_code::normal, ec);         // WS close frame
                beast::get_lowest_layer(*s).shutdown(tcp::socket::shutdown_both, ec);
                beast::get_lowest_layer(*s).close(ec);
            });
        }
    }
};

CoinbaseWs::CoinbaseWs(std::string product_id, OnMsg cb) : impl_(new Impl(std::move(product_id), std::move(cb))) {}
CoinbaseWs::~CoinbaseWs() { delete impl_; }

// The outer class methods just forward to the implementation
void CoinbaseWs::start(unsigned short port) { impl_->run(port); }
void CoinbaseWs::stop() noexcept { impl_->stop(); }