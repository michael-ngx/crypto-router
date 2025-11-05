#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <functional>

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

class HttpServer {
public:
    using HandlerFn = std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)>;

    HttpServer(boost::asio::io_context& ioc, tcp::endpoint ep, HandlerFn handler)
    : ioc_(ioc), acceptor_(ioc), handler_(std::move(handler)) {
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
        tcp::socket socket_;
        boost::beast::flat_buffer buffer_;
        HttpServer::HandlerFn handler_;
        Session(tcp::socket s, HandlerFn h) : socket_(std::move(s)), handler_(std::move(h)) {}
        void run() { do_read(); }
        void do_read() {
            auto self = shared_from_this();
            auto req = std::make_shared<http::request<http::string_body>>();
            http::async_read(socket_, buffer_, *req,
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
                    res->set(http::field::access_control_allow_methods, "GET, OPTIONS");
                    if (req->method() == http::verb::options) {
                        res->result(http::status::ok);
                        res->set(http::field::content_type, "text/plain");
                        res->body() = "";
                    }
                    http::async_write(self->socket_, *res,
                        [self, res](boost::beast::error_code, std::size_t){
                            self->do_close();
                        });
                });
        }
        void do_close() {
            boost::beast::error_code ec;
            socket_.shutdown(tcp::socket::shutdown_send, ec);
        }
    };

    void do_accept() {
        acceptor_.async_accept(
            boost::asio::make_strand(ioc_),
            [this](boost::beast::error_code ec, tcp::socket s){
                if (!ec) std::make_shared<Session>(std::move(s), handler_)->run();
                do_accept();
            });
    }

    boost::asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    HandlerFn handler_;
};