#include <spdlog/spdlog.h>

#include "zrpc.hpp"

int main()
{
    zrpc::Client cli;
    auto b = cli.call<bool>("test_method", 1, std::string("string"));
    spdlog::info("test_method: {}", b);

    std::string hello = "hello, ";
    std::string world = "world";
    auto hello_world = cli.call<std::string>("add_string", hello, world);
    spdlog::info("hello_world: {}", hello_world);

    auto sum = cli.call<int>("add_integer", 1, 2);
    spdlog::info("sum: {}", sum);

    cli.call("void_method");

    int a = cli.call<int>("foo.add1", 2);
    spdlog::info("foo.add1: {}", a);

    int r = cli.call<int>("bar.virtual_method");
    spdlog::info("bar.virtual_method: {}", r);

    r = cli.call<int>("lambda");
    spdlog::info("lambda: {}", r);
}
