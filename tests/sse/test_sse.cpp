#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <beauty/server.hpp>
#include <beauty/sse_stream.hpp>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace asio = boost::asio;

// --------------------------------------------------------------------------
std::vector<std::string> read_sse_events(int port, const std::string& path,
                                          int count,
                                          std::chrono::milliseconds timeout = 5000ms,
                                          const std::string& last_event_id = "")
{
    asio::io_context ioc;
    std::vector<std::string> events;

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::ip::tcp::socket socket(ioc);
        asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
        co_await socket.async_connect(ep, asio::use_awaitable);

        // Send HTTP request
        std::string req = "GET " + path + " HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Accept: text/event-stream\r\n"
                          "Cache-Control: no-cache\r\n";
        if (!last_event_id.empty())
            req += "Last-Event-ID: " + last_event_id + "\r\n";
        req += "\r\n";

        co_await asio::async_write(socket, asio::buffer(req), asio::use_awaitable);

        // Read response line by line
        asio::steady_timer timer(ioc);
        timer.expires_after(timeout);
        timer.async_wait([&socket](auto) { socket.close(); });

        std::string buf;
        std::array<char, 1024> tmp;

        while (socket.is_open() && (int)events.size() < count) {
            boost::system::error_code ec;
            size_t n = co_await socket.async_read_some(
                asio::buffer(tmp), asio::use_awaitable);
            buf.append(tmp.data(), n);

            // Extract lines
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.rfind("data:", 0) == 0)
                    events.push_back(line.substr(5));
                if ((int)events.size() >= count)
                    break;
            }
        }

        timer.cancel();
        socket.close();
    }, asio::detached);

    ioc.run();
    return events;
}

// --------------------------------------------------------------------------
struct SSEFixture {
    SSEFixture() {
        server.concurrency(2);
        server.add_route("/events")
            .sse(beauty::sse_handler{
                .on_connect = [](const beauty::request&,
                                 beauty::sse_stream stream,
                                 const std::string& last_id) {
                    boost::asio::co_spawn(
                        stream.get_executor(),
                        [stream, last_id]() mutable -> boost::asio::awaitable<void> {
                            int i = 0;
                            if (!last_id.empty()) {
                                try { i = std::stoi(last_id) + 1; } catch (...) {}
                            }
                            boost::asio::steady_timer timer(
                                co_await boost::asio::this_coro::executor);
                            while (stream.is_open()) {
                                stream.send_event(std::to_string(i), "msg",
                                                  "event_" + std::to_string(i));
                                i++;
                                timer.expires_after(std::chrono::milliseconds(50));
                                co_await timer.async_wait(boost::asio::use_awaitable);
                            }
                        }, boost::asio::detached);
                }
            });
        server.listen();
        port = server.port();
    }

    ~SSEFixture() { server.stop(); }

    beauty::server server;
    int port;
};

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE receives events")
{
    auto events = read_sse_events(port, "/events", 5);

    CHECK_EQ((int)events.size(), 5);
    CHECK_EQ(events[0], " event_0");
    CHECK_EQ(events[1], " event_1");
    CHECK_EQ(events[2], " event_2");
}

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE events are sequential")
{
    auto events = read_sse_events(port, "/events", 10);

    CHECK_EQ((int)events.size(), 10);
    for (int i = 0; i < (int)events.size(); i++) {
        CHECK_EQ(events[i], " event_" + std::to_string(i));
    }
}

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE resume from Last-Event-ID")
{
    auto events = read_sse_events(port, "/events", 5, 5000ms, "4");

    CHECK_EQ((int)events.size(), 5);
    CHECK_EQ(events[0], " event_5");
    CHECK_EQ(events[1], " event_6");
}

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE multiple concurrent connections")
{
    std::vector<std::vector<std::string>> results(3);
    std::vector<std::thread> threads;

    for (int i = 0; i < 3; i++) {
        threads.emplace_back([&, i]() {
            results[i] = read_sse_events(port, "/events", 5);
        });
    }

    for (auto& t : threads) t.join();

    for (auto& events : results) {
        CHECK_EQ((int)events.size(), 5);
        CHECK_EQ(events[0], " event_0");
    }
}

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE unknown route returns 404")
{
    asio::io_context ioc;
    std::string response_line;

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::ip::tcp::socket socket(ioc);
        co_await socket.async_connect(
            asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
            asio::use_awaitable);

        std::string req = "GET /nonexistent HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n\r\n";
        co_await asio::async_write(socket, asio::buffer(req), asio::use_awaitable);

        std::array<char, 1024> buf;
        size_t n = co_await socket.async_read_some(asio::buffer(buf), asio::use_awaitable);
        response_line = std::string(buf.data(), n);
        socket.close();
    }, asio::detached);

    ioc.run();
    CHECK(response_line.find("404") != std::string::npos);
}

// --------------------------------------------------------------------------
TEST_CASE_FIXTURE(SSEFixture, "SSE heartbeat is sent")
{
    // Read raw lines and check for heartbeat comment
    asio::io_context ioc;
    bool heartbeat_found = false;

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::ip::tcp::socket socket(ioc);
        co_await socket.async_connect(
            asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
            asio::use_awaitable);

        std::string req = "GET /events HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Accept: text/event-stream\r\n\r\n";
        co_await asio::async_write(socket, asio::buffer(req), asio::use_awaitable);

        asio::steady_timer timer(ioc);
        timer.expires_after(20s); // heartbeat fires at 15s
        timer.async_wait([&socket](auto) { socket.close(); });

        std::string buf;
        std::array<char, 1024> tmp;

        while (socket.is_open()) {
            size_t n = co_await socket.async_read_some(
                asio::buffer(tmp), asio::use_awaitable);
            buf.append(tmp.data(), n);
            if (buf.find(": heartbeat") != std::string::npos) {
                heartbeat_found = true;
                break;
            }
        }

        timer.cancel();
        socket.close();
    }, asio::detached);

    ioc.run();
    CHECK(heartbeat_found);
}