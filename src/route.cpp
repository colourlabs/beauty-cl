#include <beauty/route.hpp>
#include <beauty/utils.hpp>

namespace beauty {

route::route(const std::string &path, route_cb &&cb)
    : _path(path), _handler(std::move(cb)) {
  if (path.empty() || path[0] != '/') {
    throw std::runtime_error("Route path [" + path + "] must begin with '/'.");
  }

  for (auto &&p : split(path, '/')) {
    _segments.emplace_back(p);
  }

  extract_route_info();
}

route::route(const std::string &path, const beauty::route_info &route_info,
             route_cb &&cb)
    : route(path, std::move(cb)) {
  update_route_info(route_info);
}

route::route(const std::string &path, ws_handler &&handler)
    : _path(path), _handler(std::move(handler)) {
  if (path.empty() || path[0] != '/') {
    throw std::runtime_error("Route path [" + path + "] must begin with '/'.");
  }

  for (auto &&p : split(path, '/')) {
    _segments.emplace_back(p);
  }

  extract_route_info();
}

route::route(const std::string &path, sse_handler &&handler)
    : _path(path), _handler(std::move(handler)) {
  if (path.empty() || path[0] != '/') {
    throw std::runtime_error("Route path [" + path + "] must begin with '/'.");
  }

  for (auto &&p : split(path, '/')) {
    _segments.emplace_back(p);
  }

  extract_route_info();
}

bool route::match(beauty::request &req, bool is_websocket) const noexcept {
  if (is_websocket && !std::holds_alternative<ws_handler>(_handler))
    return false;
  if (!is_websocket && std::holds_alternative<ws_handler>(_handler))
    return false;

  auto target_split =
      split(std::string_view{req.target().data(), req.target().size()}, '?');
  auto request_paths = split(target_split[0], '/');

  if (_segments.size() != request_paths.size()) {
    return false;
  }

  std::string attrs =
      (target_split.size() > 1 ? std::string(target_split[1]) : "");

  for (std::size_t i = 0; i < _segments.size(); ++i) {
    auto &segment = _segments[i];
    auto &request_segment = request_paths[i];

    if (segment[0] == ':') {
      attrs += (attrs.empty() ? "" : "&") +
               std::string(&segment[1], segment.size() - 1) + "=" +
               std::string(request_segment);
    } else if (segment != request_segment) {
      return false;
    }
  }

  if (!attrs.empty()) {
    req.get_attributes() = attributes(attrs);
  }

  return true;
}

void route::extract_route_info() {
  for (auto &segment : _segments) {
    if (segment[0] == ':') {
      beauty::route_parameter rp;
      rp.name = segment.data() + 1;
      rp.in = "path";
      rp.description = "Undefined";
      rp.type = "Undefined";
      rp.format = "";
      rp.required = true;
      _route_info.route_parameters.push_back(std::move(rp));
    }
  }
}

void route::update_route_info(const beauty::route_info &route_info) {
  _route_info.description = route_info.description;
  _route_info.body = route_info.body;
  _route_info.tags = route_info.tags;

  for (const auto &param : route_info.route_parameters) {
    auto found = std::find_if(
        begin(_route_info.route_parameters), end(_route_info.route_parameters),
        [&param](const auto &info) { return param.name == info.name; });

    if (found != end(_route_info.route_parameters)) {
      found->description = param.description;
      found->type = param.type;
      found->format = param.format;
    } else {
      auto tmp = param;
      if (tmp.in.empty())
        tmp.in = "query";
      _route_info.route_parameters.emplace_back(std::move(tmp));
    }
  }
}

}