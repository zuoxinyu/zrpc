#ifndef __ZRPC_SERVER_HPP__
#define __ZRPC_SERVER_HPP__

#include "zrpc.hpp"

#include <nameof.hpp>

namespace zrpc {

using detail::fn_traits;
using detail::tp_traits;

template <typename SerdeT = Serde>
class Server {
  public:
    using DispatcherFn = std::function<const zmq::message_t(const zmq::message_t&)>;
    struct RegisteredFn {
        std::string name;
        DispatcherFn fn;
    };
    using Dispatcher = std::map<std::string, RegisteredFn>;
    using AsyncDispatcher = std::map<std::string, RegisteredFn>;

    Server(const std::string& endpoint = kEndpoint)
    {
        // avoid lossing message
        // async_pub_.set(zmq::sockopt::immediate, true);
        // event_pub_.set(zmq::sockopt::immediate, true);

        sock_.bind(endpoint);
        async_pub_.bind(kAsyncEndpoint);
        event_pub_.bind(kEventEndpoint);
        spdlog::info("svr bind to {}", kEndpoint);
        register_method("list_methods", this, &Server::list_methods);
    }

    Server(Server&) = delete;

    void serve() noexcept(false)
    {
        while (!stop_) {
            zmq::message_t identity, empty, req, resp;
            zmq::recv_result_t recv_result;

            recv_result = sock_.recv(identity, zmq::recv_flags::none);
            recv_result = sock_.recv(empty, zmq::recv_flags::none);
            recv_result = sock_.recv(req, zmq::recv_flags::none);

            std::string method;
            auto ec = SerdeT::deserialize(req, method);

            // sendmore
            sock_.send(identity, zmq::send_flags::sndmore);
            sock_.send(empty, zmq::send_flags::sndmore);

            // if (method == kHandshakeReply) {
            //     std::ignore = Serde::serialize(resp, kHandshake);
            //     auto send_result = sock_.send(resp, zmq::send_flags::none);
            // } else
            if (routes_.count(method)) {
                auto resp = call(method, req);
                auto send_result = sock_.send(resp, zmq::send_flags::none);
            } else if (async_routes_.count(method)) {
                auto resp = async_call(method, req);
                auto send_result = sock_.send(resp, zmq::send_flags::none);
            } else {
                std::ignore = Serde::serialize(resp, RPCError::kBadMethod);
                auto send_result = sock_.send(resp, zmq::send_flags::none);
            }
        }
    }

    bool stop()
    {
        stop_ = true;
        return stop_;
    }

    // `Fn` requirements:
    //   - return type: only types defined in msgpack or void
    //   - parameter types: only types defined in msgpack, no pointers
    //   - noexcept
    // `Fn` can be:
    //   - free function
    //   - free function pointer
    //   - member function
    //   - member function pointer
    //   - properly initiated generic function template
    template <typename Fn>
    inline void register_method(const char* method, Fn fn)
    {
        static_assert(detail::is_registerable<Fn>,
                      "cannot register function due to missing requirements");
        // constexpr map?
        routes_[method] = RegisteredFn{std::string(nameof::nameof_full_type<Fn>()),
                                       [this, fn](const auto& msg) { return proxy_call(fn, msg); }};
    }

    template <typename Fn, typename Class>
    inline void register_method(const char* method, Class* that, Fn fn)
    {
        static_assert(detail::is_registerable<Fn>,
                      "cannot register function due to missing requirements");
        routes_[method] =
            RegisteredFn{std::string(nameof::nameof_full_type<Fn>()),
                         [this, that, fn](const auto& msg) { return proxy_call(fn, that, msg); }};
    }

    // Fn(cb, args...)
    //   - `cb`: the callback function, must be the first argument,
    //     meets the same requirements as `Fn`
    template <typename Fn>
    void register_async_method(const char* method, Fn fn)
    {
        async_routes_[method] =
            RegisteredFn{std::string(nameof::nameof_full_type<Fn>()),
                         [this, fn](const auto& msg) { return proxy_async_call(fn, msg); }};
    }

  private:
    [[nodiscard]] auto call(const std::string& method, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        zmq::message_t ret;
        try {
            auto& fn = routes_.at(method);
            return fn.fn(msg);
        } catch (std::exception& e) {
            spdlog::error("unknown error during invoking method [{}]: {}", method, e.what());
            std::ignore = SerdeT::serialize(ret, RPCError::kUnknown);
            return ret;
        }
    }

    [[nodiscard]] auto async_call(const std::string& method, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        AsyncToken token;
        zmq::message_t ret;
        std::string _method;

        std::ignore = SerdeT::deserialize(msg, _method, token);

        try {
            auto& fn = async_routes_[method];
            return fn.fn(msg);
        } catch (std::exception& e) {
            spdlog::error("unknown error during invoking method [{}]: {}", method, e.what());
            std::ignore = SerdeT::serialize(ret, RPCError::kUnknown);
            return ret;
        }
    }

    template <typename Fn>
    [[nodiscard]] auto proxy_call(Fn fn, const zmq::message_t& msg) -> const zmq::message_t
    {
        using ArgsTuple = typename fn_traits<Fn>::tuple_type;
        using ReturnType = typename fn_traits<Fn>::return_type;
        static_assert(std::is_constructible_v<ArgsTuple>);

        std::string method;
        ArgsTuple args{};
        zmq::message_t resp;

        // deserialize args
        {
            auto de = [&](auto&... xs) { return SerdeT::deserialize(msg, method, xs...); };
            std::ignore = std::apply(de, args);
        }

        // call and get return value
        {
            if constexpr (!std::is_void_v<ReturnType>) {
                // try catch?
                auto ret = std::apply(fn, args);
                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError, ret);
            } else {
                std::apply(fn, args);
                spdlog::trace("invoke {}{} -> void", method, args);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError);
            }
        }
        return resp;
    }

    template <typename Fn, typename Class>
    [[nodiscard]] auto proxy_call(Fn fn, Class* that, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        using ArgsTuple = typename fn_traits<Fn>::tuple_type;
        using ReturnType = typename fn_traits<Fn>::return_type;
        static_assert(std::is_constructible_v<ArgsTuple>);

        auto bound_fn = [fn, that](auto&&... xs) { return std::invoke(fn, that, xs...); };
        std::string method;
        ArgsTuple args{};
        zmq::message_t resp;

        // deserialize args
        {
            auto de = [&](auto&... xs) { return SerdeT::deserialize(msg, method, xs...); };
            std::ignore = std::apply(de, args);
        }

        // call and get return value
        {
            if constexpr (!std::is_void_v<ReturnType>) {
                auto ret = std::apply(bound_fn, args);
                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError, ret);
            } else {
                std::apply(bound_fn, args);
                spdlog::trace("invoke {}{} -> {}", method, args);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError);
            }
        }
        return resp;
    }

    template <typename Fn>
    [[nodiscard]] auto proxy_async_call(Fn fn, const zmq::message_t& msg) -> const zmq::message_t
    {
        // fn(cb, int, string, float...)
        using ArgsTuple = typename fn_traits<Fn>::tuple_type;
        using ReturnType = typename fn_traits<Fn>::return_type;
        using CbFn = typename tp_traits<ArgsTuple>::car;
        using TailArgs = typename tp_traits<ArgsTuple>::cdr;
        using CbReturnType = typename fn_traits<CbFn>::return_type;

        zmq::message_t resp;
        std::string method;
        TailArgs args{};
        AsyncToken token;

        // deserialize args
        {
            auto de = [&](auto&... xs) { return SerdeT::deserialize(msg, method, token, xs...); };
            std::ignore = std::apply(de, args);
        }

        // call and get return value
        {
            CbFn cb = [this, token_back = token](auto&&... cbargs) {
                zmq::message_t pubmsg;
                std::ignore =
                    Serde::serialize(pubmsg, std::string(kAsyncFilter), token_back, cbargs...);

                auto send_result = async_pub_.send(pubmsg, zmq::send_flags::none);

                // TODO: how to handle return? recv return value from client?
                if constexpr (std::is_void_v<CbReturnType>) {
                    spdlog::trace(
                        "async callback[{}]: fn{} -> void", token_back, std::make_tuple(cbargs...));
                    return;
                } else {
                    spdlog::trace("async callback[{}]: fn{} -> {}",
                                  token_back,
                                  std::make_tuple(cbargs...),
                                  CbReturnType{});
                    return CbReturnType{};
                }
            };

            auto all_args = std::tuple_cat(std::make_tuple(std::move(cb)), args);
            if constexpr (!std::is_void_v<ReturnType>) {
                // try catch?
                auto ret = std::apply(fn, all_args);
                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError, ret);
            } else {
                std::apply(fn, all_args);
                spdlog::trace("invoke {}{} -> void", method, args);
                std::ignore = SerdeT::serialize(resp, RPCError::kNoError);
            }
        }
        return resp;
    }

    void try_handshake()
    {
        // publish a handshake message after recv the first async call from *a new client*
        // TODO: need identitify clients
        zmq::message_t hello;
        std::ignore = Serde::serialize(hello, kHandshake);
        async_pub_.send(hello, zmq::send_flags::none);
    }

    std::vector<std::string> list_methods()
    {
        std::vector<std::string> methods;
        std::transform(
            routes_.cbegin(),              //
            routes_.cend(),                //
            std::back_inserter(methods),   //
            [](const auto& pair) { return fmt::format("{}: {}", pair.first, pair.second.name); });
        std::transform(
            async_routes_.cbegin(),        //
            async_routes_.cend(),          //
            std::back_inserter(methods),   //
            [](const auto& pair) { return fmt::format("{}: {}", pair.first, pair.second.name); });
        return methods;
    }

  private:
    // (logically) immutable resources
    zmq::context_t ctx_{1};
    // socket for RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::router};
    // socket for async RPC calls
    zmq::socket_t async_pub_{ctx_, zmq::socket_type::pub};
    // socket for publishing events
    zmq::socket_t event_pub_{ctx_, zmq::socket_type::pub};

    // init once resources
    Dispatcher routes_{};
    AsyncDispatcher async_routes_{};

    // mutable states
    std::atomic<bool> stop_{false};
};

}   // namespace zrpc
#endif
