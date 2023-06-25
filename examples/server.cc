#include <thread>

#include <spdlog/spdlog.h>

#include "zrpc.hpp"

using namespace std::chrono_literals;

bool test_method(int a, std::string s)
{
    spdlog::info("test method called with args[{}, {}]", a, s);
    return false;
}

void void_method()
{
    spdlog::info("void method called");
}

template <typename T>
T generic_add(T x, T y)
{
    spdlog::info("generic_add called");
    return x + y;
}

struct Foo {
    int add1(int x)
    {
        spdlog::info("Foo.add1 called");
        return x + v;
    }
    virtual int virtual_method()
    {
        spdlog::info("Foo.virtual_method called");
        return 42;
    }

    int v = 1;
};

struct Bar : public Foo {
    int virtual_method() override
    {
        spdlog::info("Bar.virtual_method called");
        return 28;
    }
};


int main()
{
    zrpc::Server svr;
    Foo foo;
    Bar bar;
    spdlog::set_level(spdlog::level::trace);
    svr.register_method("test_method", test_method);
    svr.register_method("void_method", void_method);
    svr.register_method("add_string", generic_add<std::string>);
    svr.register_method("add_integer", generic_add<int>);
    svr.register_method("foo.add1", &foo, &Foo::add1);
    svr.register_method("bar.virtual_method", static_cast<Foo*>(&bar), &Foo::virtual_method);

    svr.serve();
    return 0;
}