#include <thread>

#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "server.hpp"
#include "types.h"

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

void default_parameter_fn(int x, int y = 0)
{
    spdlog::info("default_parameter_fn called with: {}, {}", x, y);
}

void enum_args_fn(EnumType e)
{
    spdlog::info("enum_args_fn arg: {}", magic_enum::enum_name(e));
}

void enum_class_fn(EnumClass e)
{
    spdlog::info("enum_class_fn arg: {}", e);
}

void pointer_args_fn(int* m)   // unregisterable
{
    spdlog::info("fn with pointer arg should not be registered");
}

void reference_args_fn(int& m)   // unregisterable
{
    spdlog::info("fn with pointer arg should not be registered");
}

void struct_args_fn(StructType st)
{
    spdlog::info("struct_args_fn arg: {}", st);
}

void tuple_args_fn(std::pair<int, float> p) {}

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
    auto thread_fn = [cb = std::move(cb)](int arg) {
        std::this_thread::sleep_for(3000ms);
        // this line make async callback correctly work, why?
        fmt::println("async method invoking callback: {}", arg);
        cb(arg);
    };
    std::thread(thread_fn, i).detach();
}

bool async_return_method(std::function<void(int)> cb, int i)
{
    using namespace std::chrono_literals;
    auto thread_fn = [cb = std::move(cb)](int arg) {
        std::this_thread::sleep_for(3000ms);
        cb(arg);
    };
    std::thread(thread_fn, i).detach();
    return true;
}

Pod construct_pod(int i, uint8_t c, float f, double d)
{
    return Pod{i, c, f, d};
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
    svr.register_method("add_double", generic_add<double>);
    svr.register_method("default_parameter_fn", default_parameter_fn);
    svr.register_method("foo.add1", &foo, &Foo::add1);
    svr.register_method("bar.virtual_method", static_cast<Foo*>(&bar), &Foo::virtual_method);
    svr.register_method("lambda", [bar] { return 42; });
    svr.register_method("enum_args_fn", enum_args_fn);
    svr.register_method("enum_class_fn", enum_class_fn);
    svr.register_method("struct_args_fn", struct_args_fn);
    svr.register_method("construct_pod", construct_pod);
    // svr.register_method("tuple_args_fn", tuple_args_fn);
    // svr.register_method("pointer_args_fn", pointer_args_fn);
    // svr.register_method("reference_args_fn", reference_args_fn);

    svr.register_async_method("async_method", async_method);
    svr.register_async_method("async_return_method", async_return_method);

    svr.register_method("trigger_event", [&] {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
        svr.publish_event("event1", std::string("event with string"), 10);
    });

    svr.serve();
    return 0;
}
