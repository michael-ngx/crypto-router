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

struct KrakenWs::Impl
{
    std::string host = "ws.kraken.com";
    std::string path = "/v2";
    std::string channel = "book";
    std::string depth = "1000";
    std::string symbol;
    std::string trigger;
    OnMsg on_msg;

    net::io_context ioc{1};
    net::ssl::context ssl_ctx{net::ssl::context::tls_client};
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws;
    std::atomic<bool> stop_flag{false};

    Impl(std::string sym, OnMsg cb, std::string ev)
    : symbol(std::move(sym)), trigger(std::move(ev)), on_msg(std::move(cb))
    {
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(net::ssl::verify_peer);
    }

    void run(unsigned short port)
    {
        try
        {
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host, std::to_string(port));

            ws = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(ioc, ssl_ctx);

            // TCP connect
            net::connect(beast::get_lowest_layer(*ws), results);

            // SNI
            if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
                throw beast::system_error{
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "SNI set failed"
                };
            }

            // TLS handshake
            ws->next_layer().handshake(net::ssl::stream_base::client);

            // WS handshake
            ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req){
                req.set(http::field::user_agent, "kraken-ws-connector/0.3");
                req.set(http::field::origin, "https://docs.kraken.com");
            }));
            ws->handshake(host, path);

            // Subscribe to v2 "book" channel with depth preference
            // Docs pattern: {"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":10}}
            std::string body = std::string("{\"method\":\"subscribe\",\"params\":{\"channel\":\"") + channel
                               + "\",\"symbol\":[\"" + symbol + "\"],\"depth\":" + depth + "}}";
            ws->write(net::buffer(body));
            
            // Read loop — throw on unexpected errors; break on expected shutdown
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
                    // Anything else: escalate to outer catch
                    throw beast::system_error{ec};
                }
                std::string data = beast::buffers_to_string(buffer.cdata());
                if (on_msg) on_msg(data); // Callback with received message from websocket
            }

            if (ws) {
                beast::error_code close_ec;
                ws->close(websocket::close_code::normal, close_ec);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[kraken-ws] error: " << e.what() << "\n";
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

KrakenWs::KrakenWs(std::string symbol, OnMsg cb, std::string event_trigger)
    : impl_(new Impl(std::move(symbol), std::move(cb), std::move(event_trigger))) {}

KrakenWs::~KrakenWs() { delete impl_; }

void KrakenWs::start(unsigned short port) { impl_->run(port); }
void KrakenWs::stop() noexcept { impl_->stop(); }
