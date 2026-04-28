#pragma once
#include <beauty/export.hpp>
#include <beauty/middleware.hpp>
#include <beauty/request.hpp>
#include <beauty/response.hpp>
#include <beauty/sse_handler.hpp>
#include <beauty/swagger.hpp>
#include <beauty/websocket_context.hpp>
#include <beauty/websocket_handler.hpp>

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace beauty {
using route_cb =
    std::function<void(const beauty::request &req, beauty::response &res)>;
using route_handler = std::variant<route_cb, ws_handler, sse_handler>;

class BEAUTY_EXPORT route {
public:
  explicit route(
      const std::string &path,
      route_cb &&cb = [](const auto &req, auto &res) {});
  route(
      const std::string &path, const beauty::route_info &route_info,
      route_cb &&cb = [](const auto &req, auto &res) {});
  route(const std::string &path, ws_handler &&handler);
  route(const std::string &path, sse_handler &&handler);

  route &use(middleware_fn fn) {
    _middleware.push_back(std::move(fn));
    return *this;
  }

  const std::vector<middleware_fn> &middleware() const { return _middleware; }

  bool match(beauty::request &req, bool is_websocket = false) const noexcept;

  // HTTP
  void execute(const beauty::request &req, beauty::response &res) const {
    if (auto *h = std::get_if<route_cb>(&_handler))
      (*h)(req, res);
  }

  // WebSocket
  void connect(const ws_context &ctx) const {
    if (auto *h = std::get_if<ws_handler>(&_handler))
      h->on_connect(ctx);
  }
  void receive(const ws_context &ctx, const char *data, std::size_t size,
               bool is_text) const {
    if (auto *h = std::get_if<ws_handler>(&_handler))
      h->on_receive(ctx, data, size, is_text);
  }
  void disconnect(const ws_context &ctx) const {
    if (auto *h = std::get_if<ws_handler>(&_handler))
      h->on_disconnect(ctx);
  }

  // SSE
  void sse_connect(const beauty::request &req, sse_stream &stream,
                   const std::string &last_id) const {
    if (auto *h = std::get_if<sse_handler>(&_handler))
      h->on_connect(req, stream, last_id);
  }
  void sse_disconnect(const beauty::request &req) const {
    if (auto *h = std::get_if<sse_handler>(&_handler))
      h->on_disconnect(req);
  }
  void sse_error(boost::system::error_code ec, const char *what) const {
    if (auto *h = std::get_if<sse_handler>(&_handler))
      h->on_error(ec, what);
  }

  [[nodiscard]] const std::string &path() const noexcept { return _path; }
  [[nodiscard]] const std::vector<std::string> &segments() const noexcept {
    return _segments;
  }
  [[nodiscard]] const beauty::route_info &route_info() const noexcept {
    return _route_info;
  }
  [[nodiscard]] bool is_websocket() const noexcept {
    return std::holds_alternative<ws_handler>(_handler);
  }
  [[nodiscard]] bool is_sse() const noexcept {
    return std::holds_alternative<sse_handler>(_handler);
  }

private:
  void extract_route_info();
  void update_route_info(const beauty::route_info &route_info);

private:
  std::string _path;
  std::vector<std::string> _segments;
  route_handler _handler;
  beauty::route_info _route_info;
  std::vector<middleware_fn> _middleware;
};
} // namespace beauty