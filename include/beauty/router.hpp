#pragma once

#include <beauty/middleware.hpp>
#include <beauty/export.hpp>
#include <beauty/route.hpp>

#include <boost/beast.hpp>

namespace beast = boost::beast;

namespace beauty {
// --------------------------------------------------------------------------
class BEAUTY_EXPORT router {
public:
  using routes = std::unordered_map<beast::http::verb, std::vector<route>>;

  void add_route(beast::http::verb v, route &&r);
  void use(middleware_fn fn) { _middleware.push_back(std::move(fn)); }

  routes::const_iterator find(beast::http::verb v) const noexcept {
    return _routes.find(v);
  }

  const std::vector<middleware_fn>& middleware() const { return _middleware; }

  routes::const_iterator begin() const noexcept { return _routes.begin(); }
  routes::const_iterator end() const noexcept { return _routes.end(); }

private:
  routes _routes;
  std::vector<middleware_fn> _middleware;
};

} // namespace beauty
