#include <beauty/beauty.hpp>
#include <string>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

int main() {
  beauty::server server;

  server.add_route("/").get(
      [](const auto &req, auto &res) { res.body() = "works!"; });

  server.add_route("/ssetest")
      .sse(beauty::sse_handler{.on_connect = [&](const beauty::request &,
                                                 beauty::sse_stream stream,
                                                 const std::string &last_id) {
        boost::asio::co_spawn(
            stream.get_executor(),
            [stream, last_id]() mutable -> boost::asio::awaitable<void> {
              int i = 0;
              if (!last_id.empty()) {
                try {
                  i = std::stoi(last_id) + 1;
                } catch (...) {
                }
              }

              boost::asio::steady_timer timer(
                  co_await boost::asio::this_coro::executor);

              while (stream.is_open()) {
                stream.send_event(std::to_string(i), "awesome",
                                  "yo does this work " + std::to_string(i));

                i++;

                timer.expires_after(std::chrono::seconds(1));
                co_await timer.async_wait(boost::asio::use_awaitable);
              }
            },
            boost::asio::detached);
      }});

  server.listen(8085);
  server.run();
}
