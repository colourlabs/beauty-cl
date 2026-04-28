#pragma once

#include <beauty/acceptor.hpp>
#include <beauty/application.hpp>
#include <beauty/endpoint.hpp>
#include <beauty/export.hpp>
#include <beauty/middleware.hpp>
#include <beauty/route.hpp>
#include <beauty/router.hpp>
#include <beauty/sse_handler.hpp>
#include <beauty/swagger.hpp>

#include <string>

namespace beauty {
// --------------------------------------------------------------------------
class BEAUTY_EXPORT server {
public:
  class server_route {
  public:
    server_route(server &s, std::string path)
        : _server(s), _path(std::move(path)) {}

    server_route &use(middleware_fn fn) {
      _middleware.push_back(std::move(fn));
      return *this;
    }

    // Http verbs
    server_route &get(route_cb &&cb) {
      _server.get(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &get(const route_info &route_info, route_cb &&cb) {
      _server.get(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    server_route &put(route_cb &&cb) {
      _server.put(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &put(const route_info &route_info, route_cb &&cb) {
      _server.put(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    server_route &post(route_cb &&cb) {
      _server.post(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &post(const route_info &route_info, route_cb &&cb) {
      _server.post(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    server_route &patch(route_cb &&cb) {
      _server.patch(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &patch(const route_info &route_info, route_cb &&cb) {
      _server.patch(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    server_route &options(route_cb &&cb) {
      _server.options(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &options(const route_info &route_info, route_cb &&cb) {
      _server.options(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    server_route &del(route_cb &&cb) {
      _server.del(_path, _middleware, std::move(cb));
      return *this;
    }
    server_route &del(const route_info &route_info, route_cb &&cb) {
      _server.del(_path, route_info, _middleware, std::move(cb));
      return *this;
    }

    // Websocket
    server_route &ws(ws_handler &&handler) {
      _server.ws(_path, std::move(handler));
      return *this;
    }

    // SSE
    server_route &sse(sse_handler &&handler) {
      _server.sse(_path, std::move(handler));
      return *this;
    }

  private:
    server &_server;
    std::string _path;
    std::vector<middleware_fn> _middleware;
  };

public:
  server();
  explicit server(beauty::application &app);
#if BEAUTY_ENABLE_OPENSSL
  explicit server(certificates &&c)
      : _app(beauty::application::Instance(std::move(c))) {}
#endif
  ~server();

  server(const server &) = delete;
  server &operator=(const server &) = delete;
  server(server &&) = delete;
  server &operator=(server &&) = delete;

  server &concurrency(int concurrency) {
    _concurrency = concurrency;
    return *this;
  }

  server_route add_route(const std::string &path) { return {*this, path}; }

  // Legacy API
  server &get(const std::string &path, route_cb &&cb);
  server &get(const std::string &path, const route_info &route_info,
              route_cb &&cb);
  server &put(const std::string &path, route_cb &&cb);
  server &put(const std::string &path, const route_info &route_info,
              route_cb &&cb);
  server &patch(const std::string &path, route_cb &&cb);
  server &patch(const std::string &path, const route_info &route_info,
                route_cb &&cb);
  server &post(const std::string &path, route_cb &&cb);
  server &post(const std::string &path, const route_info &route_info,
               route_cb &&cb);
  server &options(const std::string &path, route_cb &&cb);
  server &options(const std::string &path, const route_info &route_info,
                  route_cb &&cb);
  server &del(const std::string &path, route_cb &&cb);
  server &del(const std::string &path, const route_info &route_info,
              route_cb &&cb);

  // middleware-aware overloads (used by server_route)
  server &get(const std::string &path, const std::vector<middleware_fn> &m,
              route_cb &&cb);
  server &get(const std::string &path, const route_info &route_info,
              const std::vector<middleware_fn> &m, route_cb &&cb);
  server &put(const std::string &path, const std::vector<middleware_fn> &m,
              route_cb &&cb);
  server &put(const std::string &path, const route_info &route_info,
              const std::vector<middleware_fn> &m, route_cb &&cb);
  server &post(const std::string &path, const std::vector<middleware_fn> &m,
               route_cb &&cb);
  server &post(const std::string &path, const route_info &route_info,
               const std::vector<middleware_fn> &m, route_cb &&cb);
  server &patch(const std::string &path, const std::vector<middleware_fn> &m,
                route_cb &&cb);
  server &patch(const std::string &path, const route_info &route_info,
                const std::vector<middleware_fn> &m, route_cb &&cb);
  server &options(const std::string &path, const std::vector<middleware_fn> &m,
                  route_cb &&cb);
  server &options(const std::string &path, const route_info &route_info,
                  const std::vector<middleware_fn> &m, route_cb &&cb);
  server &del(const std::string &path, const std::vector<middleware_fn> &m,
              route_cb &&cb);
  server &del(const std::string &path, const route_info &route_info,
              const std::vector<middleware_fn> &m, route_cb &&cb);

  server &ws(const std::string &path, ws_handler &&handler);
  server &sse(const std::string &path, sse_handler &&handler);

  void use(middleware_fn fn);

  void listen(int port = 0, const std::string &address = "0.0.0.0");
  void stop();
  void run();
  void wait();

  const std::vector<beauty::endpoint> &endpoints() { return _endpoints; }

  const beauty::endpoint &endpoint() const {
    return _endpoints.empty() ? _empty_endpoint : _endpoints.front();
  }

  int port() const { return endpoint().port(); }

  const beauty::router &router() const noexcept { return _router; }

  const beauty::server_info &info() const noexcept { return _server_info; }
  void info(const beauty::server_info &info) { _server_info = info; }

  void enable_swagger(const char *swagger_entrypoint = "/swagger");

private:
  beauty::application &_app;
  int _concurrency{1};
  beauty::router _router;
  std::vector<std::shared_ptr<beauty::acceptor>> _acceptors;
  std::vector<beauty::endpoint> _endpoints;
  beauty::endpoint _empty_endpoint;
  beauty::server_info _server_info;
};

} // namespace beauty