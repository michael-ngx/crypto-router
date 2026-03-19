#include "ws.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <openssl/ssl.h>
#include <atomic>
#include <memory>
#include <iostream>
#include <fstream>
#include <cstdlib>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct OkxWs::Impl
{
    std::string host = "ws.okx.com";
    std::string path = "/ws/v5/public";
    std::string inst_id;
    OnMsg on_msg;

    net::io_context ioc{1};
    net::ssl::context ssl_ctx{net::ssl::context::tls_client};
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws;
    std::atomic<bool> stop_flag{false};

    Impl(std::string id, OnMsg cb)
        : inst_id(std::move(id)), on_msg(std::move(cb))
    {
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(net::ssl::verify_peer);
    }

    void run(unsigned short port)
    {
        try
        {
            // OKX WebSocket uses port 8443 (not 443)
            unsigned short okx_port = (port == 443) ? 8443 : port;
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host, std::to_string(okx_port));

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
                req.set(http::field::user_agent, "okx-ws-connector/0.1");
            }));
            ws->handshake(host, path);

            // Subscribe to books channel (400-level, snapshot + incremental)
            std::string sub = "{\"op\":\"subscribe\",\"args\":[{\"channel\":\"books\",\"instId\":\"" + inst_id + "\"}]}";
            ws->write(net::buffer(sub));

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
                static std::atomic<int> okx_msg_count{0};
                if (std::getenv("OKX_DEBUG")) {
                    int n = okx_msg_count++;
                    if (n < 2) {
                        std::cerr << "[okx-ws] msg " << (n+1) << " len=" << data.size();
                        if (data.find("\"data\"") != std::string::npos) {
                            std::cerr << " HAS_DATA";
                            // Write first data msg to /tmp for inspection
                            if (n == 1 && data.size() < 50000) {
                                std::ofstream f("/tmp/okx_sample.json");
                                if (f) f << data << std::endl;
                            }
                        }
                        std::cerr << "\n";
                    }
                }
                if (on_msg) on_msg(data);
            }

            if (ws) {
                beast::error_code close_ec;
                ws->close(websocket::close_code::normal, close_ec);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "[okx-ws] error: " << e.what() << "\n";
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

OkxWs::OkxWs(std::string inst_id, OnMsg cb)
    : impl_(new Impl(std::move(inst_id), std::move(cb))) {}

OkxWs::~OkxWs() { delete impl_; }

void OkxWs::start(unsigned short port) { impl_->run(port); }
void OkxWs::stop() noexcept { impl_->stop(); }
