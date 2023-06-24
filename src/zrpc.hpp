#include "msgpack.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <tuple>

#include <msgpack.hpp>
#include <spdlog/spdlog.h>
#include <utility>
#include <zmq.hpp>

namespace zrpc
{

namespace detail
{
// clang-format off
template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Fn> struct fn_trait_impl;

template <typename Fn> struct fn_trait : fn_trait_impl<remove_cvref_t<Fn>> {};

// ordinary function
template <typename ReturnType, typename... Args>
struct fn_trait_impl<ReturnType(Args...)> {
    using return_type = ReturnType;
    template <size_t Idx> struct args {
        using type = typename std::tuple_element<Idx, std::tuple<Args...>>::type;
    };

    using tuple_type = std::tuple<remove_cvref_t<Args>...>;
};

// function pointer
template <typename ReturnType, typename... Args>
struct fn_trait_impl<ReturnType (*)(Args...)> : fn_trait_impl<ReturnType(Args...)> {};

// member function pointer
template <typename ReturnType, typename Class, typename... Args>
struct fn_trait_impl<ReturnType (Class::*)(Args...)> : fn_trait_impl<ReturnType(Args...)> {};

template <typename Fn>
struct fn_trait_args {
    using args_type = typename std::tuple_element<0, typename fn_trait<Fn>::tuple_type>::type;
};
// clang-format on

template <typename Fn, typename ArgsTuple, size_t... i>
decltype(auto) call_fn(Fn &&fn, ArgsTuple args, std::index_sequence<i...>)
{
    return fn(std::get<i>(std::forward<ArgsTuple>(args))...);
}

template <typename Fn, typename ArgsTuple>
decltype(auto) call_fn(Fn &&fn, ArgsTuple &&args)
{
    constexpr auto argc = std::tuple_size<remove_cvref_t<ArgsTuple>>::value;
    constexpr auto sqnc = std::make_index_sequence<argc>{};
    return call_fn(std::forward<Fn>(fn), std::forward<ArgsTuple>(args), sqnc);
}
} // namespace detail

using Dispatcher =
    std::map<std::string, std::function<bool(const zmq::message_t &msg)>>;

template <typename... Args> struct Message {
    std::string func;
    std::tuple<Args...> params;

    template <typename T> void pack(T &pack) { pack(func, params); }
};

struct Serde {

    template <typename... Args>
    static std::error_code serialize(zmq::message_t &msg, Args... args)
    {
        try {
            msgpack::Packer packer;
            ([&] { packer.process(Args(args)); }(), ...);

            auto &vec = packer.vector();
            msg = zmq::message_t(vec.data(), vec.size());
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }

    template <typename... Args>
    static std::error_code deserialize(const zmq::message_t &req, Args... args)
    {
        try {
            msgpack::Unpacker unpacker(static_cast<const uint8_t *>(req.data()),
                                       static_cast<const size_t>(req.size()));
            ([&] { unpacker.process(args); }(), ...);
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }
};

class Service
{
};

static const std::string kEndpoint = "tcp://127.0.0.1:5555";
template <typename SerdeT = Serde> class Server
{

  public:
    static Dispatcher routes_;

    Server()
    {
        sock_.bind(kEndpoint);

        spdlog::info("svr bind to {}", kEndpoint);
    }

    int serve()
    {
        while (!stop_) {
            spdlog::info("start session");

            zmq::message_t req;
            auto req_result = sock_.recv(req, zmq::recv_flags::none);

            std::string method;
            SerdeT::deserialize(req, method);

            spdlog::info("svr recv: {}", method);
            call(method, req);

            bool ret = true;
            zmq::message_t resp;
            SerdeT::serialize(resp, ret);
            auto resp_result = sock_.send(resp, zmq::send_flags::none);
        }
        return 0;
    }

    bool stop()
    {
        stop_ = true;
        return true;
    }

    template <typename Fn>
    constexpr void register_method(const std::string &name, Fn fn)
    {
        routes_[name] = [this, &name = name, fn = std::forward<Fn>(fn)] //
            (const zmq::message_t &msg) -> bool {
            proxy_call(fn, msg);
            return true;
        };
    }

  private:
    void call(const std::string &method, const zmq::message_t &msg)
    {
        // constexpr map?
        auto fn = routes_.at(method);
        fn(msg);
    }

    template <typename Fn>
    decltype(auto) proxy_call(Fn fn, const zmq::message_t &msg)
    {
        using args_tuple_type = typename zrpc::detail::fn_trait<Fn>::tuple_type;

        std::string method;
        args_tuple_type args;
        // TODO: specialize deserialize for tuple type
        Serde::deserialize(msg, method, args);

        auto ret = detail::call_fn(fn, args);
        return ret;
    }

  private:
    // immutable resources
    zmq::context_t ctx_;
    zmq::socket_t sock_{ctx_, zmq::socket_type::rep};

    // mutable states
    zmq::mutable_buffer recv_buf_;
    std::atomic<bool> stop_{false};
};

template <typename SerdeT = Serde> class Client
{
  public:
    Client()
    {
        sock_.connect(kEndpoint);
        spdlog::info("cli connect to {}", kEndpoint);
    }

    template <typename... Args>
    bool call(const std::string &method, Args... args)
    {
        zmq::message_t req;
        auto ser_err = SerdeT::serialize(req, method, args...);
        auto req_result = sock_.send(req, zmq::send_flags::none);

        zmq::message_t resp;
        auto resp_result = sock_.recv(resp, zmq::recv_flags::none);

        bool ret;
        auto des_err = SerdeT::deserialize(resp, ret);

        spdlog::info("cli resp: {}", ret);

        return ret;
    }

  private:
    zmq::context_t ctx_;
    zmq::socket_t sock_{ctx_, zmq::socket_type::req};
};

} // namespace zrpc
