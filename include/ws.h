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

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
typedef ssl::stream<tcp::socket> ssl_socket;

int connect() {
    const std::string host = "stream.binance.com";
    const std::string target = "/ws";
    const std::string port = "9443";
    const std::string path = "stream.binance.com:9443";
    try
    {  
        // Create a context that uses the default paths for
        // finding CA certificates.
        std::cerr << "Finding CA..." << '\n';
        ssl::context ctx(ssl::context::tls_client);
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        //io_context is required
        net::io_context ioc;

        tcp::resolver resolver{ioc};
        websocket::stream<ssl_socket> ws{ioc, ctx};

        //DNS
        std::cerr << "DNS..." << '\n';
        auto const results = resolver.resolve(host, port);

        //TCP Connection
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

        //decorator
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-coro");
            }));
        
        // ws handshake
        ws.handshake(path, target);
        std::cout << "connected" << std::endl;

        // ws write
        std::string req = R"({
            "method": "SUBSCRIBE",
            "params": ["btcusdt@depth@100ms"],
            "id": 1
        })";

        ws.write(net::buffer(req));

        beast::flat_buffer buffer;

        using clock = std::chrono::steady_clock;

        uint64_t total = 0;
        uint64_t messages = 0;
        uint64_t bytes = 0;

        auto last = clock::now();

        // ws read
        while (true) {
            ws.read(buffer);

            ++messages;
            ++total;
            bytes += buffer.size();

            buffer.consume(buffer.size());

            auto now = clock::now();
            double elapsed =
                std::chrono::duration<double>(now - last).count();

            if (elapsed >= 1.0) {
                std::cout
                    << "msg/s: " << messages / elapsed
                    << " | KB/s: " << bytes / elapsed / 1e3
                    << " | Total: " << total
                    << '\n';

                messages = 0;
                bytes = 0;
                last = now;
            }
        }

    } catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
};

int snap_shot() {
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

        //io_context is required
        net::io_context ioc;

        tcp::resolver resolver{ioc};
        websocket::stream<ssl_socket> ws{ioc, ctx};

        //DNS
        std::cerr << "DNS..." << '\n';
        auto const results = resolver.resolve(host, port);

        //TCP Connection
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

        //decorator
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-coro");
            }));
        
        // ws handshake
        ws.handshake(host, target);
        std::cout << "connected" << std::endl;

        // ws write
        std::string req = R"({
        "id": "7d3b9b46-5f4f-4c8b-9a2d-0a8f9a4a0f5b",
        "method": "depth",
        "params": {
            "symbol": "BTCUSDT"
        }
        })";

        ws.write(net::buffer(req));

        beast::flat_buffer buffer;

        // ws read
        ws.read(buffer);
        std::cout << beast::make_printable(buffer.data()) << '\n';
        ws.close(websocket::close_code::normal);

    } catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
};