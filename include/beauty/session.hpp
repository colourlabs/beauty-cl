#pragma once

#include <beauty/exception.hpp>
#include <beauty/middleware.hpp>
#include <beauty/router.hpp>
#include <beauty/sse_session.hpp>
#include <beauty/utils.hpp>
#include <beauty/version.hpp>
#include <beauty/websocket_session.hpp>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <string>

#if BEAUTY_ENABLE_OPENSSL
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#endif

#include <memory>
#include <type_traits>

namespace asio = boost::asio;
namespace beast = boost::beast;

namespace beauty {

template <bool SSL>
class session : public std::enable_shared_from_this<session<SSL>> {
private:
  struct no_ssl {};
  using stream_type =
      std::conditional_t<SSL, asio::ssl::stream<asio::ip::tcp::socket &>,
                         no_ssl>;

public:
  template <bool U = SSL, typename std::enable_if_t<!U, int> = 0>
  session(asio::io_context &ioc, asio::ip::tcp::socket &&socket,
          const beauty::router &router)
      : _socket(std::move(socket)), _strand(asio::make_strand(ioc)),
        _router(router) {}

#if BEAUTY_ENABLE_OPENSSL
  template <bool U = SSL, typename std::enable_if_t<U, int> = 0>
  session(asio::io_context &ioc, asio::ip::tcp::socket &&socket,
          const beauty::router &router, asio::ssl::context &ctx)
      : _socket(std::move(socket)), _stream(_socket, ctx),
        _strand(asio::make_strand(ioc)), _router(router) {}
#endif

  void run() {
    if constexpr (SSL) {
#if BEAUTY_ENABLE_OPENSSL
      _stream.async_handshake(
          asio::ssl::stream_base::server,
          asio::bind_executor(_strand,
                              [me = this->shared_from_this()](auto ec) {
                                me->on_ssl_handshake(ec);
                              }));
#endif
    } else {
      do_read();
    }
  }

  void on_ssl_handshake(boost::system::error_code ec) {
    if (ec)
      return fail(ec, "failed handshake");
    do_read();
  }

  void do_read() {
    _request_parser = std::make_unique<
        beast::http::request_parser<beast::http::string_body>>();
    _request_parser->body_limit(_body_limit);
    _request_parser->eager(true);

    if constexpr (SSL) {
      beast::http::async_read(
          _stream, _buffer, *_request_parser,
          asio::bind_executor(_strand, [me = this->shared_from_this()](
                                           auto ec, auto bytes_transferred) {
            me->on_read(ec, bytes_transferred);
          }));
    } else {
      beast::http::async_read(
          _socket, _buffer, *_request_parser,
          asio::bind_executor(_strand, [me = this->shared_from_this()](
                                           auto ec, auto bytes_transferred) {
            me->on_read(ec, bytes_transferred);
          }));
    }
  }

  void on_read(boost::system::error_code ec, std::size_t) {
    if (ec == beast::http::error::end_of_stream)
      return do_close();
    if (ec)
      return fail(ec, "read");

    // Extract body directly from the parser's internal message,
    // bypassing release() which loses the body in Boost 1.83
    auto request = std::make_shared<beauty::request>();
    auto &inner = _request_parser->get();
    static_cast<beast::http::request<beast::http::string_body> &>(*request) =
        std::move(inner);

    asio::co_spawn(
        _strand,
        [me = this->shared_from_this(), request]() -> asio::awaitable<void> {
          auto response = co_await me->handle_request(std::move(*request));
          if (response) {
            if (!response->is_postponed()) {
              me->do_write(response);
            } else {
              response->on_done([me, response] { me->do_write(response); });
            }
          }
        },
        asio::detached);
  }

  void do_write(const std::shared_ptr<response> &response) {
    response->prepare_payload();

    if constexpr (SSL) {
      beast::http::async_write(
          this->_stream, *response,
          asio::bind_executor(
              this->_strand, [me = this->shared_from_this(),
                              response](auto ec, auto bytes_transferred) {
                me->on_write(ec, bytes_transferred, response->need_eof());
              }));
    } else {
      beast::http::async_write(
          this->_socket, *response,
          asio::bind_executor(
              this->_strand, [me = this->shared_from_this(),
                              response](auto ec, auto bytes_transferred) {
                me->on_write(ec, bytes_transferred, response->need_eof());
              }));
    }
  }

  void on_write(boost::system::error_code ec, std::size_t, bool close) {
    if (ec)
      return fail(ec, "write");
    if (close)
      return do_close();
    do_read();
  }

  void do_close() {
    if constexpr (SSL) {
      _stream.async_shutdown(asio::bind_executor(
          _strand, [me = this->shared_from_this()](auto ec) {
            me->on_ssl_shutdown(ec);
          }));
    } else {
      boost::system::error_code ec;
      _socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
      _socket.close();
    }
  }

  void on_ssl_shutdown(boost::system::error_code ec) {
    if (ec)
      return fail(ec, "shutdown");
    if constexpr (SSL) {
      _stream.lowest_layer().close();
    }
  }

  void set_body_limit(uint64_t limit) { _body_limit = limit; }

private:
  asio::ip::tcp::socket _socket;
  stream_type _stream = {};
  asio::strand<asio::io_context::executor_type> _strand;
  beast::flat_buffer _buffer;
  std::unique_ptr<beast::http::request_parser<beast::http::string_body>>
      _request_parser;
  uint64_t _body_limit{1024 * 1024 * 1024}; // 1GB default

  const beauty::router &_router;

  struct middleware_chain {
    size_t i = 0;
    size_t total;
    const std::vector<middleware_fn> &global;
    const std::vector<middleware_fn> &local;
    const beauty::route &route;
    beauty::request request;
    std::shared_ptr<response> res;
    beauty::next_fn next;
  };

private:
  asio::awaitable<std::shared_ptr<response>>
  handle_request(beauty::request request) {
    bool is_websocket = beast::websocket::is_upgrade(request);
    request.remote(_socket.remote_endpoint());

    auto found_method = _router.find(request.method());
    if (found_method == _router.end())
      co_return helper::bad_request(request, "Not supported HTTP-method");

    for (auto &&route : found_method->second) {
      if (route.match(request, is_websocket)) {
        if (is_websocket) {
          std::make_shared<websocket_session>(std::move(_socket), route)
              ->run(request);
          co_return nullptr;
        } else if (route.is_sse()) {
          std::make_shared<sse_session>(std::move(_socket), route)
              ->run(request);
          co_return nullptr;
        } else {
          auto res = std::make_shared<response>(beast::http::status::ok,
                                                request.version());
          res->set(beast::http::field::server, std::string("beauty-cl/") + BEAUTY_PROJECT_VERSION);
          res->keep_alive(request.keep_alive());

          const auto &global = _router.middleware();
          const auto &local = route.middleware();

          auto chain = std::make_shared<middleware_chain>(
              middleware_chain{.i = 0,
                               .total = global.size() + local.size(),
                               .global = global,
                               .local = local,
                               .route = route,
                               .request = std::move(request),
                               .res = res});

          chain->next = [chain,
                         last_i = std::make_shared<size_t>(
                             SIZE_MAX)]() mutable -> asio::awaitable<void> {
            if (chain->i == *last_i)
              co_return;
            *last_i = chain->i;
            if (chain->i < chain->global.size()) {
              co_await chain->global[chain->i++](chain->request, *chain->res,
                                                 chain->next);
            } else if (chain->i < chain->total) {
              co_await chain->local[chain->i++ - chain->global.size()](
                  chain->request, *chain->res, chain->next);
            } else {
              chain->route.execute(chain->request, *chain->res);
            }
          };

          try {
            co_await chain->next();
          } catch (const beauty::exception &ex) {
            co_return ex.create_response(chain->request);
          } catch (const std::exception &ex) {
            co_return helper::server_error(chain->request, ex.what());
          }

          co_return chain->res;
        }
      }
    }
    co_return helper::not_found(request);
  }
};

using session_http = session<false>;

#if BEAUTY_ENABLE_OPENSSL
using session_https = session<true>;
#endif

} // namespace beauty