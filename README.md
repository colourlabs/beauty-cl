<div align="center">
  <img alt="A Rose" src="https://github.com/dfleury2/beauty/raw/master/docs/rose.png" height="180" />
  <br>
  A simple HTTP server and client above <a href="https://github.com/boostorg/beast">Boost.Beast</a>
</div>
<br>

> **this is a fork of [dfleury2/beauty](https://github.com/dfleury2/beauty) with some more features :**
> - server-sent events (SSE) with resume support, heartbeat, and backpressure
> - async middleware chain (C++20 coroutines)
>
> requires C++20.

Beauty is a layer above <a href="https://github.com/boostorg/beast">Boost.Beast</a> which provide facilities to create HTTP server or client. Beauty allows the creation of synchronous or asynchronous server and client, and adds some signals and timer management based on <a href="https://github.com/boostorg/asio">Boost.Asio</a>

## Features
- HTTP or HTTPS server or client side
- WebSocket (no TLS yet) for server and client (still experimental)
- Synchronous or Asynchronous API
- Timeout support
- Postponed response from server support
- Easy routing server with placeholders
- Timers and signals support included
- Startable and stoppable application event loop
- Customizable thread pool size
- **Server-Sent Events (SSE)** with resume, heartbeat, and backpressure
- **Async middleware** chain with coroutine support for both global and local routes
- Work-in-progress: Swagger description API

## Examples

- a server

A more complete example is available in examples/server.cpp

```cpp
#include <beauty/beauty.hpp>

int main()
{
    // Create a server
    beauty::server server;

    // Add a default '/' route
    server.add_route("/")
        .get([](const auto& req, auto& res) {
            res.body() = "It's work ;) ... it works! :)";
        });

    // Add a '/person/:id' route
    server.add_route("/person/:id")
        .get([](const auto& req, auto& res) {
            auto id = req.a("id").as_string();
            res.body() = "You asked for the person id: " + id;
        });

    // Open the listening port
    server.listen(8085);
        // Listen will automatically start the loop in a separate thread

    // Wait for the server to stop
    server.wait();
}

```

- a synchronous client

```cpp
#include <beauty/beauty.hpp>

#include <iostream>

int main()
{
    // Create a client
    beauty::client client;

    // Request an URL
    auto[ec, response] = client.get("http://127.0.0.1:8085");

    // Check the result
    if (!ec) {
        if (response.is_status_ok()) {
            // Display the body received
            std::cout << response.body() << std::endl;
        } else {
            std::cout << response.status() << std::endl;
        }
    } else {
        // An error occurred
        std::cout << ec << ": " << ec.message() << std::endl;
    }
}
```
- an asynchronous client

```cpp
#include <beauty/beauty.hpp>

#include <iostream>
#include <chrono>

int main()
{
    // Create a client
    beauty::client client;

    // Request an URL
    client.get("http://127.0.0.1:8085",
               [](auto ec, auto&& response) {
                   // Check the result
                   if (!ec) {
                       if (response.is_status_ok()) {
                           // Display the body received
                           std::cout << response.body() << std::endl;
                       } else {
                           std::cout << response.status() << std::endl;
                       }
                   } else {
                       // An error occurred
                       std::cout << ec << ": " << ec.message() << std::endl;
                   }
               });

    // Need to wait a little bit to received the response
    for (int i = 0; i < 10; ++i) {
        std::cout << '.'; std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
}

```

- SSE server

```cpp
#include <beauty/beauty.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

int main()
{
    beauty::server server;

    server.add_route("/events")
        .sse(beauty::sse_handler{
            .on_connect = [](const beauty::request&,
                             beauty::sse_stream stream,
                             const std::string& last_id) {
                boost::asio::co_spawn(
                    stream.get_executor(),
                    [stream, last_id]() mutable -> boost::asio::awaitable {
                        int i = 0;
                        if (!last_id.empty()) {
                            try { i = std::stoi(last_id) + 1; } catch (...) {}
                        }

                        boost::asio::steady_timer timer(
                            co_await boost::asio::this_coro::executor);

                        while (stream.is_open()) {
                            stream.send_event(std::to_string(i), "update",
                                              "hello from event " + std::to_string(i));
                            i++;
                            timer.expires_after(std::chrono::seconds(1));
                            co_await timer.async_wait(boost::asio::use_awaitable);
                        }
                    }, boost::asio::detached);
            }
        });

    server.listen(8085);
    server.wait();
}
```

SSE streams automatically send a heartbeat every 15 seconds to keep connections alive. clients that reconnect with a `Last-Event-ID` header will resume from where they left off.

- timers

```cpp
#include <beauty/beauty.hpp>

#include <iostream>

int main()
{
    // Launch a repeatable timer each 250ms
    int timer_count = 4;
    beauty::repeat(0.250, [&timer_count]() {
            std::cout << "Tick..." << std::endl;
            if (--timer_count == 0) {
                std::cout << "Dring !" << std::endl;
                beauty::stop();
            }
        });

    // Launch a one shot timer after 600ms
    beauty::after(0.600, [] {
            std::cout << "Snooze !" << std::endl;
    });

    // Wait for the end
    beauty::wait();
}
```

- signals

```cpp
#include <beauty/beauty.hpp>

#include <iostream>

int main()
{
    // Catch the small signals
    beauty::signal({SIGUSR1, SIGUSR2}, [](int s) {
        std::cout << "Shot miss..." << std::endl;
    });

    // Catch the big one
    beauty::signal(SIGINT, [](int s) {
        std::cout << "Head shot !" << std::endl;
        beauty::stop();
    });

    // Wait for the end
    beauty::wait();
}
```

- Websocket server

Here an example of a simple chat server using websocket, use `ws://127.0.0.1:8085/chat/MyRoom` to connect
to a room named MyRoom.

```cpp
#include <beauty/beauty.hpp>

#include <string>
#include <unordered_map>

using Sessions = std::unordered_map<std::string /* UUID */, std::weak_ptr<beauty::websocket_session>>;
using Rooms = std::unordered_map<std::string /* ROOM */,  Sessions>;

Rooms rooms;

int
main(int argc, char* argv[])
{
    beauty::server server;

    server.add_route("/chat/:room")
        .ws(beauty::ws_handler{
            .on_connect = [](const beauty::ws_context& ctx) {
                rooms[ctx.attributes["room"].as_string()][ctx.uuid] = ctx.ws_session;
            },
            .on_receive = [](const beauty::ws_context& ctx, const char* data, std::size_t size, bool is_text) {
                for (auto& [uuid, session] : rooms[ctx.attributes["room"].as_string()]) {
                    if (auto s = session.lock(); s) {
                        s->send(std::string(data, size));
                    }
                }
            },
            .on_disconnect = [](const beauty::ws_context& ctx) {
                for (auto& [name, room] : rooms) {
                    room.erase(ctx.uuid);
                }
            }
        });

    server.listen(8085);

    beauty::wait();
}

```


- Websocket client

Here an example of a simple client that connect to the previous chat server,
use `ws://127.0.0.1:8085/chat/MyRoom` to connect to a room named MyRoom.

```cpp
int
main(int argc, char* argv[])
{
    beauty::client client;

    client.ws(argv[1], beauty::ws_handler{
        .on_connect = [](const beauty::ws_context& ctx) {
            std::cout << "--- Connected --- to: " << ctx.remote_endpoint << std::endl;
        },
        .on_receive = [](const beauty::ws_context& ctx, const char* data, std::size_t size, bool is_text) {
            std::cout << "--- Received:\n";
            std::cout.write(data, size);
            std::cout << "\n---" << std::endl;

        },
        .on_disconnect = [&client](const beauty::ws_context& ctx) {
            std::cout << "--- Disconnected ---" << std::endl;
        },
        .on_error = [&client](boost::system::error_code ec, const char* what) {
            std::cout << "--- Error: " << ec << ", " << ec.message() << ": " << what << std::endl;

            std::cout << "Retrying connection on error in 1s..." << std::endl;
            beauty::after(1.0, [&client] {
                std::cout << "Trying connection..." << std::endl;
                client.ws_connect();
            });
        }
    });

    std::cout << "> ";
    std::cout.flush();
    for(std::string line; getline(std::cin, line);) {
        if (line == "q") break;
        std::cout << "> ";
        std::cout.flush();
        client.ws_send(std::move(line));
    }
}
```

- async middleware

coroutine based global and route based middleware


```cpp
#include <beauty/beauty.hpp>

int main()
{
  beauty::server server;

  // example server wide middleware: reject requests without a valid API key
  server.use([](auto& req, auto& res, auto next) -> boost::asio::awaitable<void> {
      if (req["X-Api-Key"] != "secret") {
        res.result(boost::beast::http::status::unauthorized);
        res.body() = "Invalid API key";
        co_return;
      }

      co_await next();
  });

  server.add_route("/hello")
      .get([](const auto& req, auto& res) {
          res.body() = "Hello!";
      });

  server.add_route("/")
      .use([](auto &req, auto &res, auto next) -> boost::asio::awaitable<void> {
        std::cout << "wow";
        co_await next();
      })
      .get([](const auto& req, auto& res) {
          res.body() = "Hello!";
      });

  server.listen(8085);
  server.wait();
}
```

Further examples can be found into the binaries directory at the root of the project.

## Build

Beauty depends Boost.Beast and OpenSsl. You can rely on Conan 2.x to get the package or
only the FindPackage from CMake.

### Linux

For Conan, you need to provide a profile, here, using default as profile.

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty/
conan install . -pr:h default -pr:b default -b missing -of build
cmake -S . -B build --preset conan-release
cmake --build build --preset conan-release
```

If you do not want to use Conan, you can try:

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Hope your the dependencies are found on your Linux.

If you want to disable openssl:
```shell
conan install . -o "&:openssl=False" -pr:h default -pr:b default -b missing -of build
```

In case you want to build and run unit tests, you can use the following command:

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty/
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
cd build/tests/
ctest
```

To build the examples, you can use the following command:

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty/
cmake -S . -B build -DBEAUTY_BUILD_EXAMPLES=ON
cmake --build build
cd build/examples/
./beauty_application # or any other example
```

### Linux Makefile

For those who want just a simple Makefile without bothering with dependency management,
in the `docs` directory, there is an example of a simple one-shot Makefile to create a
library archive to be used in another project. This Makefile must used (and moved) at the
project root.

There are a Makefile provided in the `docs` directory:

```shell
cd docs && make -j10 VERBOSE=1
```


### Windows

You must have a valid profile for VS Build. At this time conan center provide only pre-built packages
for VS2019 (compiler.version = 16) and x86_64 mode (arch=x86_64). Boost and OpenSSL can be compiled
automatically for VS2022.

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty
conan install . -o "beauty/*:openssl=False" -o "beauty/*:shared=False"-pr vs2022 -b missing -of build
cmake -S . -B build --preset conan-default
cmake --build build --config Release
```

The binaries will be created in the `build\examples\Release` directory.

For a Full dynamic compilation without SSL:

```shell
git clone https://github.com/dfleury2/beauty.git
cd beauty
conan install . -o "beauty/*:openssl=False" -o "*:shared=True"-pr vs2022 -b missing -of build
cmake -S . -B build --preset conan-default
cmake --build build --config Release
```

To be improved...
