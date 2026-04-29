#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <beauty/middleware.hpp>
#include <beauty/request.hpp>
#include <beauty/response.hpp>
#include <beauty/router.hpp>
#include <doctest/doctest.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>

#include <optional>
#include <stdexcept>

// --------------------------------------------------------------------------
// test helpers
// --------------------------------------------------------------------------

template <typename T> T sync_run(boost::asio::awaitable<T> awt) {
  boost::asio::io_context ioc;
  std::optional<T> result;
  std::exception_ptr eptr;
  boost::asio::co_spawn(
      ioc,
      [&]() -> boost::asio::awaitable<void> {
        result = co_await std::move(awt);
      },
      [&](std::exception_ptr ep) { eptr = ep; });
  ioc.run();
  if (eptr)
    std::rethrow_exception(eptr);
  if (!result)
    throw std::runtime_error("coroutine did not produce a value");
  return std::move(*result);
}

void sync_run(boost::asio::awaitable<void> awt) {
  boost::asio::io_context ioc;
  std::exception_ptr eptr;
  boost::asio::co_spawn(
      ioc, [&]() -> boost::asio::awaitable<void> { co_await std::move(awt); },
      [&](std::exception_ptr ep) { eptr = ep; });
  ioc.run();
  if (eptr)
    std::rethrow_exception(eptr);
}

void run_middleware(
    const std::vector<beauty::middleware_fn> &global,
    const std::vector<beauty::middleware_fn> &local, beauty::request &req,
    beauty::response &res,
    std::function<void(beauty::request &, beauty::response &)> handler = {}) {
  size_t i = 0;
  size_t total = global.size() + local.size();
  bool handler_called = false;
  std::function<boost::asio::awaitable<void>()> next;
  next = [&]() -> boost::asio::awaitable<void> {
    if (i < global.size())
      co_await global[i++](req, res, next);
    else if (i < total)
      co_await local[i++ - global.size()](req, res, next);
    else if (handler && !handler_called) {
      handler_called = true;
      handler(req, res);
    }
  };
  sync_run(next());
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

  run_middleware(router.middleware(), {}, req, res);

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
        co_return;
      });

  router.use([&second_ran](auto &req, auto &res,
                           auto next) -> boost::asio::awaitable<void> {
    second_ran = true;
    co_await next();
  });

  run_middleware(router.middleware(), {}, req, res);

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

  run_middleware(router.middleware(), {}, req, res);

  CHECK_EQ(res[boost::beast::http::field::access_control_allow_origin], "*");
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware exceptions propagate") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  router.use(
      [](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        throw std::runtime_error("Something went wrong");
        co_await next();
      });

  bool caught = false;
  try {
    run_middleware(router.middleware(), {}, req, res);
  } catch (const std::exception &) {
    caught = true;
  }
  CHECK(caught);
}

// --------------------------------------------------------------------------
TEST_CASE("Route-based middleware isolation") {
  beauty::router router;

  bool global_ran = false;
  router.use([&](auto &, auto &, auto next) -> boost::asio::awaitable<void> {
    global_ran = true;
    co_await next();
  });

  bool route_ran = false;
  bool handler_ran = false;

  beauty::route my_route("/test", {},
                         [&](auto &, auto &) { handler_ran = true; });
  my_route.use([&](auto &, auto &, auto next) -> boost::asio::awaitable<void> {
    route_ran = true;
    co_await next();
  });

  beauty::request req;
  beauty::response res;

  run_middleware(router.middleware(), my_route.middleware(), req, res,
                 [&](auto &r, auto &s) { my_route.execute(r, s); });

  CHECK(global_ran);
  CHECK(route_ran);
  CHECK(handler_ran);
}

// --------------------------------------------------------------------------
TEST_CASE("Global middleware runs before route middleware") {
  beauty::router router;
  beauty::route my_route("/test", {}, [](auto &, auto &) {});

  std::vector<std::string> order;

  router.use([&](auto &, auto &, auto next) -> boost::asio::awaitable<void> {
    order.push_back("global");
    co_await next();
  });

  my_route.use([&](auto &, auto &, auto next) -> boost::asio::awaitable<void> {
    order.push_back("route");
    co_await next();
  });

  beauty::request req;
  beauty::response res;

  run_middleware(router.middleware(), my_route.middleware(), req, res);

  REQUIRE_EQ(order.size(), 2);
  CHECK_EQ(order[0], "global");
  CHECK_EQ(order[1], "route");
}

// --------------------------------------------------------------------------
TEST_CASE("Empty middleware chain calls handler directly") {
  beauty::router router; // no middleware added

  bool handler_ran = false;
  beauty::request req;
  beauty::response res;

  run_middleware({}, {}, req, res, [&](auto &, auto &) { handler_ran = true; });

  CHECK(handler_ran);
}

// --------------------------------------------------------------------------
TEST_CASE("Middleware calling next twice does not double-execute handler") {
  beauty::router router;
  beauty::request req;
  beauty::response res;

  router.use([](auto &, auto &, auto next) -> boost::asio::awaitable<void> {
    co_await next();
    co_await next(); // second call — should be a no-op once i >= total
  });

  int handler_count = 0;

  run_middleware(router.middleware(), {}, req, res,
                 [&](auto &, auto &) { handler_count++; });

  // handler should only run once — the second next() finds i >= total
  // and handler is not re-invoked because run_middleware uses i as a counter
  CHECK_EQ(handler_count, 1);
}