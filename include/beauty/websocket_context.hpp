#pragma once

#include <beauty/attributes.hpp>
#include <beauty/endpoint.hpp>

#include <memory>
#include <string>

namespace beauty {
class websocket_session;

// --------------------------------------------------------------------------
struct ws_context {
  beauty::endpoint remote_endpoint;
  beauty::endpoint local_endpoint;
  std::string uuid;
  std::weak_ptr<websocket_session> ws_session;

  std::string target;
  beauty::attributes attributes;

  std::string route_path;
};

} // namespace beauty
