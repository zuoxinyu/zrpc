#include <spdlog/spdlog.h>
#include <thread>

#include "client.hpp"

enum EnumType {
    kState1 = 1,
    kState2 = 2,
    kState3 = 3,
};

int main()
{
    using namespace std::chrono_literals;

    spdlog::set_level(spdlog::level::trace);

    zrpc::Client cli;
    auto b = cli.call<bool>("test_method", 1, std::string("string"));

    std::string hello = "hello, ";
    std::string world = "world";

    cli.call("void_method");
    std::ignore = cli.call<std::string>("add_string", hello, world);
    std::ignore = cli.call<int>("add_integer", 1, 2);
    std::ignore = cli.call<double>("add_double", 3.14, 2.76);
    std::ignore = cli.call<int>("foo.add1", 2);
    std::ignore = cli.call<int>("bar.virtual_method");
    std::ignore = cli.call<int>("lambda");
    cli.call("default_parameter_fn", 1);
    cli.call("default_parameter_fn", 1, 2);

    auto cb = [](int i) { spdlog::info("async_method callback: {}", i); };
    cli.async_call("async_method", cb, 1);
    cli.async_call("async_method", cb, 2);
    cli.async_call("async_method", cb, 3);
    cli.call("enum_args_fn", kState2);
    auto recursive_cb = [&](int i) { cli.async_call("async_method", cb, 3); };
    cli.async_call("async_method", recursive_cb, 2);

    while (cli.poll()) {
        ;
    }

    // std::this_thread::sleep_for(5s);
}
