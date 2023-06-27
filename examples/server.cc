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

void pointer_args_fn(int* m)
{
    spdlog::info("fn with pointer arg should not be registered");
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

void async_method(std::function<void(int)> cb, int i)

{
    using namespace std::chrono_literals;
    auto thread_fn = [cb](int arg) {
        std::this_thread::sleep_for(5000ms);
        cb(arg);
    };
    std::thread(thread_fn, i).detach();
}


int main()
{
    spdlog::set_level(spdlog::level::trace);

    zrpc::Server svr;
    Foo foo;
    Bar bar;
    svr.register_method("test_method", test_method);
    svr.register_method("void_method", void_method);
    svr.register_method("add_string", generic_add<std::string>);
    svr.register_method("add_integer", generic_add<int>);
    svr.register_method("foo.add1", &foo, &Foo::add1);
    svr.register_method("bar.virtual_method", static_cast<Foo*>(&bar), &Foo::virtual_method);
    svr.register_method("lambda", [] { return 42; });
    // svr.register_method("pointer_args_fn", pointer_args_fn);

    svr.register_async_method("async_method", async_method);

    svr.serve();
    return 0;
}
