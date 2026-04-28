#include <beauty/sse_session.hpp>
#include <beauty/sse_stream.hpp>
#include <sstream>

namespace beauty {

void sse_stream::send(const std::string &id, const std::string &event,
                      const std::string &data) {
  if (!_impl || data.empty())
    return;

  std::ostringstream out;

  if (!id.empty()) {
    out << "id: " << id << "\n";
  }

  if (!event.empty()) {
    out << "event: " << event << "\n";
  }

  std::istringstream in(data);
  std::string line;
  while (std::getline(in, line)) {
    out << "data: " << line << "\n";
  }

  out << "\n";

  _impl->send(out.str());
}

void sse_stream::send(const std::string &data) { send("", "", data); }

void sse_stream::send_event(const std::string &id, const std::string &event, const std::string &data) {
  send(id, event, data);
}

void sse_stream::send_with_id(const std::string &id, const std::string &data) {
  send(id, "", data);
}

void sse_stream::close() {
  if (_impl) {
    _impl->close();
  }
}

bool sse_stream::is_open() const noexcept { return _impl && _impl->is_open(); }

boost::asio::strand<boost::asio::any_io_executor> sse_stream::get_executor() const {
    return _impl->get_executor();
}

} // namespace beauty