#include <beauty/beauty.hpp>
#include <boost/asio/awaitable.hpp>

int main() {
  beauty::server server;

  server.use(
      [](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        if (req.target() == "/hello") {
          co_await next();
          co_return;
        }

        if (req["X-Api-Key"] != "secret") {
          res.result(boost::beast::http::status::unauthorized);
          res.body() = "Invalid API key";
          co_return;
        }

        co_await next();
      });

  // The /hello route will now bypass the check
  server.add_route("/hello")
      .use([](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        std::cout << "wow";
        co_await next();
      })
      .get([](const auto &req, auto &res) { res.body() = "Hello!"; });

  // The root route still requires the API key
  server.add_route("/").get(
      [](const auto &req, auto &res) { res.body() = "admin area"; });

  server.listen(8085);
  server.wait();
}