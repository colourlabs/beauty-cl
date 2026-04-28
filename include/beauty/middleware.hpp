#pragma once

#include <beauty/request.hpp>
#include <beauty/response.hpp>

#include <boost/asio/awaitable.hpp>
#include <functional>

namespace beauty {

using next_fn = std::function<boost::asio::awaitable<void>()>;
using middleware_fn = std::function<boost::asio::awaitable<void>(request&, response&, next_fn)>;

} // namespace beauty