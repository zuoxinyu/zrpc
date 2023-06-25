#ifndef _ZRPC_HPP_
#define _ZRPC_HPP_
#include <atomic>
#include <functional>
#include <map>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <zmq.hpp>

#include "msgpack.hpp"

namespace zrpc {
namespace detail {
template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Fn>
struct fn_trait_impl;

template <typename Fn>
struct fn_trait : fn_trait_impl<remove_cvref_t<Fn>> {};

// ordinary function
template <typename ReturnType, typename... Args>
struct fn_trait_impl<ReturnType(Args...)> {
    using return_type = ReturnType;

    template <size_t Idx>
    struct args {
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

template <typename T>
struct is_serializable_type {
    static constexpr bool value = true;
};

template <typename T>
constexpr bool is_pointer_type()
{
    return std::is_pointer_v<remove_cvref_t<T>>;
}

template <typename T>
struct any_pointer_type {
    static constexpr bool value = is_pointer_type<T>();
};

template <typename... Args>
struct any_pointer_type<std::tuple<Args...>> {
    static constexpr bool value = (is_pointer_type<Args>() or ...);
};

static_assert(any_pointer_type<std::tuple<int*, float>>::value);
static_assert(!any_pointer_type<std::tuple<int, float>>::value);

}   // namespace detail

enum class RPCError {
    kNoError = 0,
    kBadMethod,
    kSerdeFailed,
};

struct RPCErrorCategory : public std::error_category {
    const char* name() const noexcept override { return "zrpc"; }

    std::string message(int ec) const override
    {
        switch (static_cast<zrpc::RPCError>(ec)) {
        case RPCError::kNoError: return "(no error)"; break;
        case RPCError::kBadMethod: return "bad request"; break;
        case RPCError::kSerdeFailed: return "bad payload"; break;
        default: return "(unrecognized error)";
        }
    }
};

const RPCErrorCategory theRPCErrorCategory{};

inline std::error_code make_error_code(zrpc::RPCError e)
{
    return {static_cast<int>(e), theRPCErrorCategory};
}
}   // namespace zrpc

namespace std {
template <>
struct is_error_code_enum<zrpc::RPCError> : public true_type {};
}   // namespace std

namespace zrpc {
static const std::string kEndpoint = "tcp://127.0.0.1:5555";
using detail::fn_trait;

// default [De]serialize implementation
// TODO: make it a custom point in a better way
struct Serde {
    template <typename... Args>
    [[nodiscard]] static auto serialize(zmq::message_t& msg, const Args&... args)
        -> std::error_code   // TODO: exception instead?
    {
        try {
            msgpack::Packer packer;
            ([&] { packer.process(Args(args)); }(), ...);
            msg = {packer.vector().data(), packer.vector().size()};
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }

    template <typename... Args>
    [[nodiscard]] static auto deserialize(const zmq::message_t& req, Args&... args)
        -> std::error_code   // TODO: exception instead?
    {
        try {
            msgpack::Unpacker unpacker(static_cast<const uint8_t*>(req.data()),
                                       static_cast<const size_t>(req.size()));
            ([&] { unpacker.process(args); }(), ...);
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }
};

// calling convention:
template <typename SerdeT = Serde>
class Server {
  public:
    using DispatcherFn = std::function<const zmq::message_t(const zmq::message_t& msg)>;
    using Dispatcher = std::map<std::string, DispatcherFn>;
    using DispatcherAsyncFn = std::pair<std::function<void(const zmq::message_t& msg)>,
                                        std::function<void(const zmq::message_t& msg)>>;
    using AsyncDispatch = std::map<std::string, DispatcherAsyncFn>;

    Server(const std::string& endpoint = kEndpoint)
    {
        sock_.bind(endpoint);
        spdlog::info("svr bind to {}", kEndpoint);
    }

    Server(Server&) = delete;

    int serve()
    {
        while (!stop_) {
            zmq::message_t req;
            auto recv_result = sock_.recv(req, zmq::recv_flags::none);
            if (!recv_result) {
                spdlog::error("failed to recv request: zmq::socket_t::recv()");
                // TODO
            }

            std::string method;
            std::error_code ec = SerdeT::deserialize(req, method);
            if (ec) {
                // TODO
            }

            auto resp = call(method.c_str(), req);
            sock_.send(resp, zmq::send_flags::none);
        }
        return 0;
    }

    bool stop()
    {
        stop_ = true;
        return stop_;
    }

    // `Fn` requires:
    //   - return type: only types defined in msgpack
    //   - parameter types: only types defined in msgpack
    //   - generic function must be explicity initiated
    template <typename Fn>
    inline void register_method(const char* method, Fn fn)
    {
        static_assert(detail::is_serializable_type<typename fn_trait<Fn>::return_type>::value);
        static_assert(!detail::any_pointer_type<typename fn_trait<Fn>::tuple_type>::value);
        // static_assert(detail::is_serializable_type<typename fn_trait<Fn>::args_type>::value);
        // constexpr map?
        routes_[method] = [this, method, fn](const auto& msg) { return proxy_call(fn, msg); };
    }

    template <typename Fn, typename Class>
    inline void register_method(const char* method, Class* that, Fn fn)
    {
        routes_[method] = [this, that, method, fn](const auto& msg) {
            return proxy_call(fn, that, msg);
        };
    }

  private:
    [[nodiscard]] auto call(const char* method, const zmq::message_t& msg) -> const zmq::message_t
    {
        try {
            auto fn = routes_.at(method);
            return fn(msg);
        } catch (std::exception& e) {
            spdlog::error("method: [{}] not found", method);
            return zmq::message_t{std::string(e.what())};
        }
    }

    template <typename Fn>
    [[nodiscard]] auto proxy_call(Fn fn, const zmq::message_t& msg) -> const zmq::message_t
    {
        using ArgsTuple = typename zrpc::fn_trait<Fn>::tuple_type;
        static_assert(std::is_constructible_v<ArgsTuple>);

        std::string method;
        ArgsTuple args{};
        zmq::message_t resp;

        // deserialize args
        {
            auto de = [&](auto&... xs) { return Serde::deserialize(msg, method, xs...); };
            auto ec = std::apply(de, args);
            if (ec) {
                // TODO
            }
        }

        // call and get return value
        {
            if constexpr (!std::is_void_v<typename fn_trait<Fn>::return_type>) {
                auto ret = std::apply(fn, args);
                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                auto ec = Serde::serialize(resp, /*RPCError::kNoError*/ ret);
                if (ec) {
                    // TODO
                }
            } else {
                std::apply(fn, args);
            }
        }
        return resp;
    }

    template <typename Fn, typename Class>
    [[nodiscard]] auto proxy_call(Fn fn, Class* that, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        using ArgsTuple = typename zrpc::fn_trait<Fn>::tuple_type;
        static_assert(std::is_constructible_v<ArgsTuple>);

        std::string method;
        ArgsTuple args{};
        zmq::message_t resp;

        // deserialize args
        {
            auto de = [&](auto&... xs) { return SerdeT::deserialize(msg, method, xs...); };
            auto ec = std::apply(de, args);
            if (ec) {
                // TODO
            }
        }

        // call and get return value
        {
            auto bound_fn = [fn, that](auto&&... xs) { return std::invoke(fn, that, xs...); };
            if constexpr (!std::is_void_v<typename fn_trait<Fn>::return_type>) {
                auto ret = std::apply(bound_fn, args);

                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                auto ec = SerdeT::serialize(resp, ret);
                if (ec) {
                    // TODO
                }
            } else {
                std::apply(bound_fn, args);
            }
        }
        return resp;
    }

  private:
    // (logically) immutable resources
    zmq::context_t ctx_;
    // socket for RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::rep};
    // socket for async RPC calls
    zmq::socket_t async_sock_{ctx_, zmq::socket_type::pub};
    // socket for publishing events
    zmq::socket_t pub_{ctx_, zmq::socket_type::pub};

    // init once resources
    Dispatcher routes_;

    // mutable states
    std::atomic<bool> stop_{false};
};

template <typename SerdeT = Serde>
class Client {
  public:
    using Event = std::string;
    using EventHandler = std::function<void(Event)>;
    using EventDispatcher = std::map<Event, EventHandler>;

    Client(const std::string& endpoint = kEndpoint)
    {
        sock_.connect(endpoint);
        spdlog::info("cli connect to {}", kEndpoint);
    }

    Client(Client&) = delete;

    // calling convention:
    //   - request: [method, args...]
    //   - response: [error_code, return value]
    // requires:
    //   - `ReturnType`: is_default_constructible
    //   - `Args...`: `PackableObject`, e.g.: has a `pack()` member
    // usage:
    //   - must specify return type via `call<int>()` / `call<std::string>()` etc.
    template <typename ReturnType = void, typename... Args>
    auto call(const char* method, Args... args) -> ReturnType
    {
        zmq::message_t req, resp;

        {
            std::string str_method = method;
            auto ec = SerdeT::serialize(req, str_method, args...);
            auto req_result = sock_.send(req, zmq::send_flags::none);
        }

        {
            auto resp_result = sock_.recv(resp, zmq::recv_flags::none);
            if constexpr (std::is_void_v<ReturnType>) {
                return;
            } else {
                static_assert(std::is_constructible_v<ReturnType>);
                ReturnType ret{};
                auto ec = SerdeT::deserialize(resp, ret);
                return ret;
            }
        }
    }

    // register an event, thread safety?
    inline auto register_event(const Event& event, EventHandler handler)
    {
        events_map_[event] = std::move(handler);
    }

  private:
    // thread for handling async results and server events
    void poll_thread()
    {
        while (!stop_) {
            zmq::message_t msg;
            auto recv_result = sub_.recv(msg, zmq::recv_flags::none);
        }
    }

  private:
    // (logically) immutable resources
    zmq::context_t ctx_;
    // socket for sync RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::req};
    // socket for async RPC calls
    zmq::socket_t async_sock_{ctx_, zmq::socket_type::sub};
    // socket for subscribing events
    zmq::socket_t sub_{ctx_, zmq::socket_type::sub};

    // init once resources
    EventDispatcher events_map_;

    // mutable states
    std::atomic<bool> stop_{false};
};

}   // namespace zrpc
#endif   // #ifndef _ZRPC_HPP_
