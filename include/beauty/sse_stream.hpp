#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <string>

namespace beauty {

class sse_stream_impl;

class sse_stream {
public:
  explicit sse_stream(std::shared_ptr<sse_stream_impl> impl)
      : _impl(std::move(impl)) {}

  // standard send (data only)
  void send(const std::string &data);

  // explicit methods for event or ID
  void send_event(const std::string &id, const std::string &event, const std::string &data);
  void send_with_id(const std::string &id, const std::string &data);

  // full control (Master)
  void send(const std::string &id, const std::string &event,
            const std::string &data);

  void close();
  bool is_open() const noexcept;

  boost::asio::strand<boost::asio::any_io_executor> get_executor() const;

private:
  std::shared_ptr<sse_stream_impl> _impl;
};

} // namespace beauty