#include <thread>

#include <spdlog/spdlog.h>

#include "zrpc.hpp"

using namespace std::chrono_literals;

bool test_method(int a, std::string s)
{
    spdlog::info("test method called with args[{}, {}]", a, s);
    return true;
}

int main()
{
    zrpc::Server svr;
    svr.register_method("test_method", test_method);
    std::thread svr_thread([&] { svr.serve(); });

    std::this_thread::sleep_for(1s);

    zrpc::Client cli;
    cli.call("test_method", 1, std::string("str"));

    svr.stop();
    if (svr_thread.joinable())
        svr_thread.join();
    return 0;
}
