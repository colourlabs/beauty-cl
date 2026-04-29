#pragma once

#include <beauty/request.hpp>
#include <beauty/route.hpp>
#include <beauty/sse_stream.hpp>

#include <boost/asio.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace asio = boost::asio;
namespace beast = boost::beast;

namespace beauty {

class sse_stream_impl : public std::enable_shared_from_this<sse_stream_impl> {
public:
  sse_stream_impl(asio::ip::tcp::socket socket,
                  asio::strand<asio::any_io_executor> strand)
      : _socket(std::move(socket)), _strand(std::move(strand)),
        _timer(_strand) {}

  void send(std::string payload) {
    asio::post(_strand,
               [me = shared_from_this(), buf = std::move(payload)]() mutable {
                 // backpressure
                 if (me->_queue.size() > 1024) {
                   me->close();
                   return;
                 }

                 bool idle = me->_queue.empty();
                 me->_queue.push_back(std::move(buf));
                 if (idle)
                   me->do_write();
               });
  }

  void close() {
    asio::post(_strand, [me = shared_from_this()] {
      me->_timer.cancel();
      beast::error_code ec;
      me->_socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
      me->_socket.close(ec);
    });
  }

  auto get_executor() const { return _strand; }

  bool is_open() const { return _socket.is_open(); }

  void start_heartbeat() {
    _timer.expires_after(std::chrono::seconds(15));
    _timer.async_wait(asio::bind_executor(
        _strand, [me = shared_from_this()](beast::error_code ec) {
          if (!ec && me->is_open()) {
            me->send(": heartbeat\n\n");
            me->start_heartbeat();
          }
        }));
  }

  template <typename F> void start_drain(F &&on_disconnect) {
    _drain_buf.resize(128);
    _socket.async_read_some(
        asio::buffer(_drain_buf),
        asio::bind_executor(
            _strand,
            [me = shared_from_this(), cb = std::forward<F>(on_disconnect)](
                beast::error_code ec, std::size_t) mutable {
              if (ec) {
                cb(ec); // EOF or reset = client gone
                return;
              }
              me->start_drain(std::move(cb)); // keep draining
            }));
  }

private:
  void do_write() {
    if (_queue.empty())
      return;

    asio::async_write(
        _socket, asio::buffer(_queue.front()),
        asio::bind_executor(_strand, [me = shared_from_this()](
                                         beast::error_code ec, std::size_t) {
          if (ec) {
            me->_socket.close();
            return;
          }
          me->_queue.pop_front();
          me->do_write();
        }));
  }

  asio::ip::tcp::socket _socket;
  asio::strand<asio::any_io_executor> _strand;
  asio::steady_timer _timer;
  std::deque<std::string> _queue;
  std::vector<char> _drain_buf;
};

class sse_session : public std::enable_shared_from_this<sse_session> {
public:
  sse_session(asio::ip::tcp::socket socket, const beauty::route &route)
      : _route(route), _strand(asio::make_strand(socket.get_executor())),
        _impl(std::make_shared<sse_stream_impl>(std::move(socket), _strand)) {}

  void run(beauty::request req) {
    _request = std::move(req);

    std::string last_event_id;
    auto it = _request.find("Last-Event-ID");
    if (it != _request.end()) {
      last_event_id = std::string(it->value());
    }

    beast::http::response<beast::http::empty_body> res{beast::http::status::ok,
                                                       _request.version()};
    res.set(beast::http::field::content_type, "text/event-stream");
    res.set(beast::http::field::cache_control, "no-cache");
    res.set(beast::http::field::connection, "keep-alive");
    res.set("X-Accel-Buffering", "no");
    res.prepare_payload();

    std::ostringstream oss;
    oss << res;
    _impl->send(oss.str());

    _impl->start_heartbeat();

    sse_stream stream(_impl);
    try {
      _route.sse_connect(_request, stream, last_event_id);
    } catch (...) {
      _impl->close();
      return;
    }

    _impl->start_drain([me = shared_from_this()](beast::error_code ec) {
      me->_route.sse_disconnect(me->_request);
      if (ec && ec != beast::http::error::end_of_stream &&
          ec != asio::error::eof) {
        me->_route.sse_error(ec, "drain");
      }
    });
  }

private:
  beauty::request _request;
  const beauty::route &_route;
  asio::strand<asio::any_io_executor> _strand;
  std::shared_ptr<sse_stream_impl> _impl;
};

} // namespace beauty