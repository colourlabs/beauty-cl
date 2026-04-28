#pragma once

#include <beauty/request.hpp>
#include <functional>
#include <string>

namespace beauty {

class sse_stream;

// --------------------------------------------------------------------------
// SSE callbacks
// --------------------------------------------------------------------------
using sse_on_connect_cb    = std::function<void(const request&, sse_stream&, const std::string&)>;
using sse_on_disconnect_cb = std::function<void(const request&)>;
using sse_on_error_cb      = std::function<void(boost::system::error_code, const char* what)>;

// --------------------------------------------------------------------------
struct sse_handler {
    sse_on_connect_cb    on_connect    = [](const request&, sse_stream&, const std::string&) {};
    sse_on_disconnect_cb on_disconnect = [](const request&) {};
    sse_on_error_cb      on_error      = [](boost::system::error_code, const char*) {};
};

};
