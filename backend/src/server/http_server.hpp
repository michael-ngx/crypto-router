#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <functional>

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

class HttpServer {
public:
    using HandlerFn = std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)>;

    HttpServer(boost::asio::io_context& ioc, ssl::context& ssl_ctx, tcp::endpoint ep, HandlerFn handler)
    : ioc_(ioc), ssl_ctx_(ssl_ctx), acceptor_(ioc), handler_(std::move(handler)) {
        boost::beast::error_code ec;
        acceptor_.open(ep.protocol(), ec);
        if (ec) throw std::runtime_error("open: " + ec.message());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec) throw std::runtime_error("set_option: " + ec.message());
        acceptor_.bind(ep, ec);
        if (ec) throw std::runtime_error("bind: " + ec.message());
        acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) throw std::runtime_error("listen: " + ec.message());
    }

    void run() { do_accept(); }

private:
    struct Session : public std::enable_shared_from_this<Session> {
        boost::beast::ssl_stream<tcp::socket> stream_;
        boost::beast::flat_buffer buffer_;
        HttpServer::HandlerFn handler_;
        Session(tcp::socket s, ssl::context& ssl_ctx, HandlerFn h)
            : stream_(std::move(s), ssl_ctx), handler_(std::move(h)) {}
        void run() { do_handshake(); }
        void do_handshake() {
            auto self = shared_from_this();
            stream_.async_handshake(ssl::stream_base::server,
                [self](boost::beast::error_code ec) {
                    if (ec) return;
                    self->do_read();
                });
        }
        void do_read() {
            auto self = shared_from_this();
            auto req = std::make_shared<http::request<http::string_body>>();
            http::async_read(stream_, buffer_, *req,
                [self, req](boost::beast::error_code ec, std::size_t){
                    if (ec == http::error::end_of_stream) return self->do_close();
                    if (ec) return;
                    auto res = std::make_shared<http::response<http::string_body>>();
                    res->version(req->version());
                    res->keep_alive(false);
                    self->handler_(*req, *res);
                    // CORS
                    res->set(http::field::access_control_allow_origin, "*");
                    res->set(http::field::access_control_allow_headers, "*");
                    res->set(http::field::access_control_allow_methods, "GET, POST, PATCH, OPTIONS");
                    if (req->method() == http::verb::options) {
                        res->result(http::status::ok);
                        res->set(http::field::content_type, "text/plain");
                        res->body() = "";
                    }
                    http::async_write(self->stream_, *res,
                        [self, res](boost::beast::error_code, std::size_t){
                            self->do_close();
                        });
                });
        }
        void do_close() {
            auto self = shared_from_this();
            stream_.async_shutdown([self](boost::beast::error_code ec) {
                if (ec == boost::asio::error::eof ||
                    ec == boost::asio::ssl::error::stream_truncated) {
                    ec = {};
                }
                boost::beast::error_code ignored;
                self->stream_.next_layer().shutdown(tcp::socket::shutdown_send, ignored);
            });
        }
    };

    void do_accept() {
        acceptor_.async_accept(
            boost::asio::make_strand(ioc_),
            [this](boost::beast::error_code ec, tcp::socket s){
                if (!ec) std::make_shared<Session>(std::move(s), ssl_ctx_, handler_)->run();
                do_accept();
            });
    }

    boost::asio::io_context& ioc_;
    ssl::context& ssl_ctx_;
    tcp::acceptor acceptor_;
    HandlerFn handler_;
};
