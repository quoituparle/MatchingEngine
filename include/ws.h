#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h> // for SSL_set_tlsext_host_name

#include <cstdlib>
#include <iostream>
#include <string>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <boost/property_tree/json_parser.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
typedef ssl::stream<tcp::socket> ssl_socket;

using json = boost::property_tree::ptree;

struct OrderBook {
    std::map<uint64_t, uint64_t> bids, asks; // price -> quantity, scaled by 1e8
    uint64_t last_update_id = 0;
};

inline json parse_json(const std::string& message) {
    std::istringstream input(message);
    json result;
    boost::property_tree::read_json(input, result);
    return result;
}

// "82411.88000000" -> 8241188000000, without floating-point rounding.
inline uint64_t to_fixed(std::string text) {
    const auto dot = text.find('.');
    const auto decimals = dot == std::string::npos ? 0 : text.size() - dot - 1;
    if (decimals > 8) throw std::runtime_error("Too many decimal places");
    if (dot != std::string::npos) text.erase(dot, 1);
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid price or quantity");
    text.append(8 - decimals, '0');
    return std::stoull(text);
}

inline std::string show_price(uint64_t value) {
    auto fraction = std::to_string(value % 100000000);
    return std::to_string(value / 100000000) + "." +
           std::string(8 - fraction.size(), '0') + fraction;
}

inline void update_levels(std::map<uint64_t, uint64_t>& side, const json& levels) {
    for (const auto& level : levels) {
        if (level.second.size() != 2) throw std::runtime_error("Invalid price level");
        const auto price = to_fixed(level.second.front().second.data());
        const auto quantity = to_fixed(level.second.back().second.data());
        if (price == 0) throw std::runtime_error("Invalid zero price");
        if (quantity == 0) side.erase(price);
        else side[price] = quantity; // Set the new total; do not add it.
    }
}

inline bool update_book(OrderBook& book, const json& event) {
    const auto U = event.get<uint64_t>("U");
    const auto u = event.get<uint64_t>("u");
    if (U > u) throw std::runtime_error("Invalid update ID range");
    if (u <= book.last_update_id) return false;
    if (U > book.last_update_id + 1) {
        book = OrderBook{};
        throw std::runtime_error("Missed depth updates; restart synchronization");
    }
    update_levels(book.bids, event.get_child("b"));
    update_levels(book.asks, event.get_child("a"));
    book.last_update_id = u;
    return true;
}

inline void open_websocket(websocket::stream<ssl_socket>& ws,
                           const std::string& host, const std::string& port,
                           const std::string& target) {
    tcp::resolver resolver{ws.get_executor()};
    // Resolve the host.
    std::cerr << "DNS..." << '\n';
    auto const results = resolver.resolve(host, port);

    // Open the TCP connection.
    std::cerr << "TCP..." << '\n';
    net::connect(
        ws.next_layer().next_layer(),
        results
    );

    // Hostname verification
    ws.next_layer().set_verify_callback(
        ssl::host_name_verification(host)
    );

    // SNI
    std::cerr << "SNI..." << '\n';
    if (!SSL_set_tlsext_host_name(
            ws.next_layer().native_handle(),
            host.c_str()))
    {
        throw beast::system_error(
            beast::error_code(
                static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()
            )
        );
    }

    ws.next_layer().handshake(
        ssl::stream_base::client
    );

    // Set the WebSocket user agent.
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req)
        {
            req.set(http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-client-coro");
        }));

    // Complete the WebSocket handshake.
    ws.handshake(host + ":" + port, target);
    std::cout << "connected" << std::endl;
}

// Fetch into snapshot when supplied; otherwise print the response.
inline int snap_shot(json* snapshot = nullptr) {
    const std::string host   = "ws-api.binance.com";
    const std::string port   = "443";
    const std::string target = "/ws-api/v3";

    try
    {
        // Create a context that uses the default paths for
        // finding CA certificates.
        std::cerr << "Finding CA..." << '\n';
        ssl::context ctx(ssl::context::tls_client);
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        // Use one I/O context for this connection.
        net::io_context ioc;

        websocket::stream<ssl_socket> ws{ioc, ctx};

        open_websocket(ws, host, port, target);

        // Send the request.
        std::string req = R"({
        "id": "7d3b9b46-5f4f-4c8b-9a2d-0a8f9a4a0f5b",
        "method": "depth",
        "params": {
            "symbol": "BTCUSDT",
            "limit": 5000
        }
        })";

        ws.write(net::buffer(req));

        beast::flat_buffer buffer;

        // Read the snapshot response.
        ws.read(buffer);
        const auto message = beast::buffers_to_string(buffer.data());
        const auto reply = parse_json(message);
        if (reply.get<int>("status", 0) != 200 || reply.get_child_optional("error"))
            throw std::runtime_error("Snapshot request failed: " + message);
        if (snapshot) *snapshot = reply.get_child("result");
        else std::cout << message << '\n';
        beast::error_code ignored;
        ws.close(websocket::close_code::normal, ignored);

    } catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
};

inline int connect(std::size_t max_updates = 0) {
    const std::string host = "stream.binance.com";
    const std::string target = "/ws";
    const std::string port = "9443";
    for (int attempt = 0; attempt < 3; ++attempt) {
        try
        {
            // Create a context that uses the default paths for
            // finding CA certificates.
            std::cerr << "Finding CA..." << '\n';
            ssl::context ctx(ssl::context::tls_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);

            // Use one I/O context for this connection.
            net::io_context ioc;

            websocket::stream<ssl_socket> ws{ioc, ctx};

            open_websocket(ws, host, port, target);

            // Send the request.
            std::string req = R"({
                "method": "SUBSCRIBE",
                "params": ["btcusdt@depth@100ms"],
                "id": 1
            })";

            ws.write(net::buffer(req));

            beast::flat_buffer buffer;

            OrderBook book;
            std::deque<json> events;
            std::future<json> snapshot_job;
            bool snapshot_loaded = false;
            std::size_t updates = 0;
            auto last_print = std::chrono::steady_clock::now() - std::chrono::seconds(1);

            while (true) {
                ws.read(buffer);
                auto event = parse_json(beast::buffers_to_string(buffer.data()));
                buffer.consume(buffer.size()); // Start the next message with an empty buffer.

                if (event.get_child_optional("code") || event.get_child_optional("error"))
                    throw std::runtime_error("Depth subscription failed");
                const auto type = event.get<std::string>("e", "");
                if (type == "serverShutdown") throw std::runtime_error("Server shutdown");
                if (type != "depthUpdate") continue; // Skip the subscription reply.
                if (event.get<std::string>("s") != "BTCUSDT")
                    throw std::runtime_error("Unexpected symbol");
                events.push_back(std::move(event));
                if (events.size() > 4096) throw std::runtime_error("Depth buffer is full");

                // 1. Buffer depth events while fetching the snapshot in the background.
                if (!snapshot_loaded) {
                    if (!snapshot_job.valid()) {
                        snapshot_job = std::async(std::launch::async, [] {
                            json snapshot;
                            if (snap_shot(&snapshot) != EXIT_SUCCESS)
                                throw std::runtime_error("Could not get snapshot");
                            return snapshot;
                        });
                    }
                    if (snapshot_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                        continue;
                    auto snapshot = snapshot_job.get();
                    const auto L = snapshot.get<uint64_t>("lastUpdateId");
                    if (L < events.front().get<uint64_t>("U")) continue; // Retry an old snapshot.

                    // 2. Initialize both sides from the snapshot at update ID L.
                    update_levels(book.bids, snapshot.get_child("bids"));
                    update_levels(book.asks, snapshot.get_child("asks"));
                    book.last_update_id = L;
                    snapshot_loaded = true;
                    std::cout << "snapshot loaded: " << L << '\n';
                }

                // 3. Replay buffered events, then apply live events in the same loop.
                while (!events.empty()) {
                    const bool applied = update_book(book, events.front());
                    events.pop_front();
                    if (!applied) continue;
                    ++updates;
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last_print >= std::chrono::seconds(1)) {
                        std::cout << "book update=" << book.last_update_id;
                        if (!book.bids.empty())
                            std::cout << " bid=" << show_price(book.bids.rbegin()->first);
                        if (!book.asks.empty())
                            std::cout << " ask=" << show_price(book.asks.begin()->first);
                        std::cout << " levels=" << book.bids.size() << '/' << book.asks.size() << '\n';
                        last_print = now;
                    }
                    if (max_updates != 0 && updates >= max_updates) {
                        beast::error_code ignored;
                        ws.close(websocket::close_code::normal, ignored);
                        return EXIT_SUCCESS;
                    }
                }
            }

        } catch(const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            if (attempt == 2) return EXIT_FAILURE;
            std::cerr << "Reconnecting and requesting a fresh snapshot...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return EXIT_FAILURE;
};