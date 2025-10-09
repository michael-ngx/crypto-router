#include <uwebsockets/App.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <sstream>

#include "order.hpp"
#include "storage.hpp"

using json = nlohmann::json;

// Simple ID generator
static std::atomic<uint64_t> g_id{1};
static std::string next_id() {
    uint64_t v = g_id.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream os; os << "ord-" << v; return os.str();
}

struct WsUserData { };

int main() {
    std::unique_ptr<Storage> store(make_memory_storage());

    // Topic names
    const std::string kTicksTopic = "ticks";

    // Create the app
    uWS::App app;

    // REST: POST /orders
    app.post("/orders", [&store](auto *res, auto *req) {
        // Must not run async tasks after returning; use onData to read body
        res->onData([res, &store](std::string_view body, bool last) {
            if (!last) return; // in this MVP assume fits in one chunk
            try {
                auto j = json::parse(body);
                Order o;
                o.id     = next_id();
                o.symbol = j.at("symbol").get<std::string>();
                o.side   = j.at("side").get<std::string>();
                o.type   = j.value("type", std::string("LIMIT"));
                o.price  = j.at("price").get<double>();
                o.qty    = j.at("qty").get<double>();
                o.status = "NEW";
                o.ts_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
                store->add_order(o);
                json resp = {
                    {"ok", true},
                    {"order", {
                        {"id", o.id}, {"symbol", o.symbol}, {"side", o.side},
                        {"type", o.type}, {"price", o.price}, {"qty", o.qty},
                        {"status", o.status}, {"ts_ns", o.ts_ns}
                    }}
                };
                std::string s = resp.dump();
                res->writeStatus("200 OK").writeHeader("content-type", "application/json").end(s);
            } catch (const std::exception& e) {
                json err = {{"ok", false}, {"error", e.what()}};
                res->writeStatus("400 Bad Request").writeHeader("content-type", "application/json").end(err.dump());
            }
        });
    });

    // REST: GET /orders
    app.get("/orders", [&store](auto *res, auto *req){
        auto vec = store->list_orders();
        json j = json::array();
        for (auto &o : vec) {
            j.push_back({
                {"id", o.id}, {"symbol", o.symbol}, {"side", o.side},
                {"type", o.type}, {"price", o.price}, {"qty", o.qty},
                {"status", o.status}, {"ts_ns", o.ts_ns}
            });
        }
        std::string s = j.dump();
        res->writeHeader("content-type", "application/json").end(s);
    });

    // WebSocket: /ws
    app.ws<WsUserData>("/ws",
        {
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 1 * 1024 * 1024,
            .idleTimeout = 120,
            .open = [kTicksTopic](auto *ws) {
                // Subscribe each connection to ticks topic
                ws->subscribe(kTicksTopic);
            },
            .message = [](auto *ws, std::string_view msg, uWS::OpCode code) {
                // Echo pings or handle client messages if needed
            },
            .close = [](auto *ws, int code, std::string_view message) {
            }
        }
    );

    // Start listening
    int port = 9001;
    app.listen(port, [port](auto *token){
        if (token) std::printf("[router] listening on http://localhost:%d\n", port);
    });

    // Start a tick broadcaster on another thread that posts into the app loop
    std::thread ticker([&app, kTicksTopic]{
        // Random walk around mid
        double mid = 60000.0;
        std::mt19937_64 rng{std::random_device{}()};
        std::normal_distribution<double> step(0.0, 5.0);

        // Get the primary loop for thread-safe defers
        uWS::Loop *loop = uWS::Loop::get();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            mid += step(rng);
            double bid = mid - 2.0;
            double ask = mid + 2.0;
            json tick = {
                {"symbol", "BTC-USD"},
                {"bid", bid}, {"ask", ask},
                {"ts_ms", (std::int64_t) (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count())}
            };
            std::string payload = tick.dump();
            // Defer publish onto the app loop to be thread-safe
            loop->defer([&app, kTicksTopic, payload]() {
                app.publish(kTicksTopic, payload, uWS::OpCode::TEXT);
            });
        }
    });

    app.run();
    ticker.join();
    return 0;
}