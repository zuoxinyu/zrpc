#include <spdlog/spdlog.h>
#include <thread>

#include "client.hpp"
#include "types.h"

int main()
{
    using namespace std::chrono_literals;

    spdlog::set_level(spdlog::level::trace);

    zrpc::Client cli;
    auto methods = cli.call<std::vector<std::string>>("list_methods");

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
    cli.call("enum_class_fn", EnumClass::kStep2);
    cli.call("struct_args_fn", StructType{1, "error msg"});
    auto recursive_cb = [&](int i) { cli.async_call("async_method", cb, 3); };
    cli.async_call("async_method", recursive_cb, 2);
    cli.async_call<bool>("async_return_method", cb, 2);
    cli.register_event("event1", [](std::string s, int i) -> bool {
        spdlog::info("recv event: event1 with args: {}, {}", s, i);
        return true;
    });

    cli.call("trigger_event");
    cli.call("trigger_event");

    try {
        cli.call("nonexist");
    } catch (zrpc::RPCError ec) {
        spdlog::error("failed: {}", static_cast<uint32_t>(ec));
    }

    while (cli.poll()) {}

    // std::this_thread::sleep_for(5s);
}
