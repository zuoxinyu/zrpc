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

}   // namespace detail

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

template <typename SerdeT = Serde>
class Server {
  public:
    using DispatcherFn = std::function<const zmq::message_t(const zmq::message_t& msg)>;
    using Dispatcher = std::map<std::string, DispatcherFn>;

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
                spdlog::error("bad request: failed to deserialize method: {}", ec.message());
                sock_.send(req, zmq::send_flags::none);
                continue;
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
    void register_method(const char* method, Fn fn)
    {
        // constexpr map?
        routes_[method] = [this, method, fn](const auto& msg) { return proxy_call(fn, msg); };
    }

    template <typename Fn, typename Class>
    void register_method(const char* method, Class* that, Fn fn)
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
        using args_tuple_t = typename zrpc::fn_trait<Fn>::tuple_type;

        std::string method;
        args_tuple_t args;
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
                auto ec = Serde::serialize(resp, ret);
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
        using args_tuple_t = typename zrpc::fn_trait<Fn>::tuple_type;

        std::string method;
        args_tuple_t args;
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
            auto bound_fn = [fn, that](auto&&... xs) { return std::invoke(fn, that, xs...); };
            if constexpr (!std::is_void_v<typename fn_trait<Fn>::return_type>) {
                auto ret = std::apply(bound_fn, args);

                spdlog::trace("invoke {}{} -> {}", method, args, ret);
                auto ec = Serde::serialize(resp, ret);
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
    // immutable resources
    zmq::context_t ctx_;
    zmq::socket_t sock_{ctx_, zmq::socket_type::rep};

    // mutable states
    Dispatcher routes_;
    std::atomic<bool> stop_{false};
};

template <typename SerdeT = Serde>
class Client {
  public:
    Client(const std::string& endpoint = kEndpoint)
    {
        sock_.connect(endpoint);
        spdlog::info("cli connect to {}", kEndpoint);
    }

    Client(Client&) = delete;

    // requires:
    //   - must specify return type via `call<int>()` / `call<std::string>()` etc.
    template <typename ReturnType = void, typename... Args>
    auto call(const std::string& method, Args... args) -> ReturnType
    {
        zmq::message_t req, resp;

        {
            auto ec = SerdeT::serialize(req, method, args...);
            auto req_result = sock_.send(req, zmq::send_flags::none);
        }

        {
            auto resp_result = sock_.recv(resp, zmq::recv_flags::none);
            if constexpr (std::is_void_v<ReturnType>) {
                return;
            } else {
                ReturnType ret;
                auto ec = SerdeT::deserialize(resp, ret);
                return ret;
            }
        }
    }

  private:
    zmq::context_t ctx_;
    zmq::socket_t sock_{ctx_, zmq::socket_type::req};
};

}   // namespace zrpc
