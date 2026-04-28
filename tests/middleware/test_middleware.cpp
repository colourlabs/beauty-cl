#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <beauty/middleware.hpp>
#include <beauty/request.hpp>
#include <beauty/response.hpp>
#include <beauty/router.hpp>
#include <doctest/doctest.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>

template <typename T> T sync_run(boost::asio::awaitable<T> awt) {
  boost::asio::io_context ioc;
  T result;
  boost::asio::co_spawn(
      ioc,
      [&]() -> boost::asio::awaitable<void> {
        result = co_await std::move(awt);
      },
      boost::asio::detached);
  ioc.run();
  return result;
}

void sync_run(boost::asio::awaitable<void> awt) {
  boost::asio::io_context ioc;
  boost::asio::co_spawn(ioc, std::move(awt), boost::asio::detached);
  ioc.run();
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware executes in order") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  std::vector<int> order;

  router.use([&order](auto &req, auto &res,
                      auto next) -> boost::asio::awaitable<void> {
    order.push_back(1);
    co_await next();
    order.push_back(3);
  });

  router.use([&order](auto &req, auto &res,
                      auto next) -> boost::asio::awaitable<void> {
    order.push_back(2);
    co_await next();
  });

  auto &chain = router.middleware();
  size_t i = 0;
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
    if (i < chain.size())
      co_await chain[i++](req, res, next);
  };

  sync_run(next());

  CHECK_EQ(order.size(), 3);
  CHECK_EQ(order[0], 1);
  CHECK_EQ(order[1], 2);
  CHECK_EQ(order[2], 3);
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware can short-circuit") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  bool second_ran = false;

  router.use(
      [](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        res.result(boost::beast::http::status::unauthorized);
        co_return; // don't call next
      });

  router.use([&second_ran](auto &req, auto &res,
                           auto next) -> boost::asio::awaitable<void> {
    second_ran = true;
    co_await next();
  });

  auto &chain = router.middleware();
  size_t i = 0;
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
    if (i < chain.size())
      co_await chain[i++](req, res, next);
  };

  sync_run(next());

  CHECK_FALSE(second_ran);
  CHECK_EQ(res.result(), boost::beast::http::status::unauthorized);
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware can modify response headers") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  router.use(
      [](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        res.set(boost::beast::http::field::access_control_allow_origin, "*");
        co_await next();
      });

  auto &chain = router.middleware();
  size_t i = 0;
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
    if (i < chain.size())
      co_await chain[i++](req, res, next);
  };

  sync_run(next());

  CHECK_EQ(res[boost::beast::http::field::access_control_allow_origin], "*");
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware exceptions are caught") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  router.use([](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
    throw std::runtime_error("Something went wrong");
    co_await next();
  });

  auto &chain = router.middleware();
  size_t i = 0;
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
    if (i < chain.size())
      co_await chain[i++](req, res, next);
  };

  CHECK_NOTHROW(sync_run(next()));
}

// --------------------------------------------------------------------------
TEST_CASE("Route-based middleware isolation") {
  beauty::router router;
  
  bool global_ran = false;
  router.use([&](auto&, auto&, auto next) -> boost::asio::awaitable<void> {
      global_ran = true;
      co_await next();
  });

  bool route_ran = false;
  beauty::route my_route("/test", {}, [](auto&, auto&){});
  my_route.use([&](auto&, auto&, auto next) -> boost::asio::awaitable<void> {
      route_ran = true;
      co_await next();
  });

  beauty::request req;
  beauty::response res;
  const auto& global = router.middleware();
  const auto& local = my_route.middleware();
  
  size_t i = 0;
  size_t total = global.size() + local.size();
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
      if (i < global.size()) co_await global[i++](req, res, next);
      else if (i < total) co_await local[i++ - global.size()](req, res, next);
      else my_route.execute(req, res);
  };

  sync_run(next());

  CHECK(global_ran);
  CHECK(route_ran);
}