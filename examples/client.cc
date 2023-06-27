#include <spdlog/spdlog.h>

#include "zrpc.hpp"

int main()
{
    using namespace std::chrono_literals;

    spdlog::set_level(spdlog::level::trace);

    zrpc::Client cli;
    auto b = cli.call<bool>("test_method", 1, std::string("string"));

    std::string hello = "hello, ";
    std::string world = "world";

    cli.call("void_method");
    auto s = cli.call<std::string>("add_string", hello, world);
    auto x = cli.call<int>("add_integer", 1, 2);
    auto a = cli.call<int>("foo.add1", 2);
    auto r = cli.call<int>("bar.virtual_method");
    auto p = cli.call<int>("lambda");
    auto cb = [](int i) { spdlog::info("async_method callback: {}", i); };
    cli.async_call("async_method", cb, 1);

    while (cli.poll()) {
        ;
    }

    // std::this_thread::sleep_for(5s);
}
