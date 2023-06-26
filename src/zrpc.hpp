#ifndef _ZRPC_HPP_
#define _ZRPC_HPP_
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

#include "msgpack.hpp"
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <zmq.hpp>

namespace zrpc {
namespace detail {
template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename>
struct fn_traits;

template <typename Fn>   // overloaded operator () (e.g. std::function)
struct fn_traits : fn_traits<decltype(&std::remove_reference_t<Fn>::operator())> {};

template <typename ReturnType, typename... Args>   // Free functions
struct fn_traits<ReturnType(Args...)> {
    using tuple_type = std::tuple<Args...>;

    static constexpr std::size_t arity = std::tuple_size<tuple_type>::value;

    template <std::size_t N>
    using argument_type = typename std::tuple_element<N, tuple_type>::type;

    using return_type = ReturnType;
};

template <typename ReturnType, typename... Args>   // Function pointers
struct fn_traits<ReturnType (*)(Args...)> : fn_traits<ReturnType(Args...)> {};

// member functions
template <typename ReturnType, typename Class, typename... Args>
struct fn_traits<ReturnType (Class::*)(Args...)> : fn_traits<ReturnType(Args...)> {
    using class_type = Class;
};

// const member functions (and lambda's operator() gets redirected here)
template <typename ReturnType, typename Class, typename... Args>
struct fn_traits<ReturnType (Class::*)(Args...) const> : fn_traits<ReturnType (Class::*)(Args...)> {
};

// TODO: whether can be serialized/deserialized to msgpack
template <typename T>
struct is_serializable_type {
    static constexpr bool value = true;
};

template <typename T>
constexpr inline bool is_pointer_type = std::is_pointer_v<remove_cvref_t<T>>;

template <typename T>
struct any_pointer_type {
    static constexpr inline bool value = is_pointer_type<T>;
};

template <typename... Args>
struct any_pointer_type<std::tuple<Args...>> {
    static constexpr inline bool value = (is_pointer_type<Args> or ...);
};

static_assert(any_pointer_type<std::tuple<int*, float>>::value);
static_assert(!any_pointer_type<std::tuple<int, float>>::value);

template <typename Fn>
constexpr inline bool is_registerable =
    is_serializable_type<typename fn_traits<Fn>::return_type>::value and
    !any_pointer_type<typename fn_traits<Fn>::tuple_type>::value;

}   // namespace detail

enum class RPCError : uint32_t {
    kNoError = 0,

    kBadPayload = 400,
    kBadMethod = 404,

    kUnknown = 500,
};

struct RPCErrorCategory : public std::error_category {
    inline const char* name() const noexcept override { return "zrpc"; }

    std::string message(int ec) const override
    {
        switch (static_cast<zrpc::RPCError>(ec)) {
        case RPCError::kNoError: return "(no error)"; break;
        case RPCError::kBadPayload: return "bad payload"; break;
        case RPCError::kBadMethod: return "bad method"; break;
        case RPCError::kUnknown:
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

namespace msgpack {
template <>
inline void Packer::pack_type<zrpc::RPCError>(const zrpc::RPCError& e)
{
    pack_type(static_cast<const uint32_t>(e));
}
template <>
inline void Unpacker::unpack_type<zrpc::RPCError>(zrpc::RPCError& e)
{
    uint32_t u;
    unpack_type(u);
    e = static_cast<zrpc::RPCError>(u);
}
}   // namespace msgpack

namespace std {
template <>
struct is_error_code_enum<zrpc::RPCError> : public true_type {};
}   // namespace std

namespace zrpc {

template <typename Fn>
using fn_traits = detail::fn_traits<Fn>;

static const std::string kEndpoint = "tcp://127.0.0.1:5555";

// default [De]serialize implementation
// TODO: make it a custom point in a better way
struct Serde {
    template <typename... Args>
    [[nodiscard]] static auto serialize(zmq::message_t& msg, const Args&... args)
        -> std::error_code   // TODO: exception instead?
    {
        try {
            msgpack::Packer packer;
            (packer.process(args), ...);
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
            (unpacker.process(args), ...);
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }
};

template <typename SerdeT = Serde>
class Server {
  public:
    using DispatcherFn = std::function<const zmq::message_t(const zmq::message_t&)>;
    using AsyncDispatcherFn = std::pair<DispatcherFn, std::function<void(const zmq::message_t&)>>;
    using Dispatcher = std::map<std::string, DispatcherFn>;
    using AsyncDispatcher = std::map<std::string, AsyncDispatcherFn>;

    Server(const std::string& endpoint = kEndpoint)
    {
        sock_.bind(endpoint);
        spdlog::info("svr bind to {}", kEndpoint);
    }

    Server(Server&) = delete;

    int serve() noexcept(false)
    {
        while (!stop_) {
            zmq::message_t req;
            auto recv_result = sock_.recv(req, zmq::recv_flags::none);

            std::string method;
            auto ec = SerdeT::deserialize(req, method);
            if (async_routes_.count(method)) {
                auto resp = async_call(method, req);
                auto send_result = sock_.send(resp, zmq::send_flags::none);
            } else {
                auto resp = call(method, req);
                auto send_result = sock_.send(resp, zmq::send_flags::none);
            }
        }
        return 0;
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
        routes_[method] = [this, method, fn](const auto& msg) { return proxy_call(fn, msg); };
    }

    template <typename Fn, typename Class>
    inline void register_method(const char* method, Class* that, Fn fn)
    {
        static_assert(detail::is_registerable<Fn>,
                      "cannot register function due to missing requirements");
        routes_[method] = [this, that, fn](const auto& msg) { return proxy_call(fn, that, msg); };
    }

    template <typename Fn, typename Callback>
    void register_async_method(const char* method, Fn fn, Callback cb)
    {}

  private:
    [[nodiscard]] auto call(const std::string& method, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        zmq::message_t ret;
        try {
            auto fn = routes_.at(method);
            return fn(msg);
        } catch (std::out_of_range& e) {
            spdlog::error("method: [{}] not found", method);
            std::ignore = SerdeT::serialize(ret, RPCError::kBadMethod);
        } catch (std::exception& e) {
            spdlog::error("unknown error during invoking method [{}]: {}", method, e.what());
            std::ignore = SerdeT::serialize(ret, RPCError::kUnknown);
        }

        return ret;
    }

    [[nodiscard]] auto async_call(const std::string& method, const zmq::message_t& msg)
        -> const zmq::message_t
    {
        zmq::message_t ret;
        try {
            auto [fn, cb] = async_routes_.at(method);
            return fn(msg);
        } catch (std::out_of_range& e) {
            spdlog::error("method: [{}] not found", method);
            std::ignore = SerdeT::serialize(ret, RPCError::kBadMethod);
        } catch (std::exception& e) {
            spdlog::error("unknown error during invoking method [{}]: {}", method, e.what());
            std::ignore = SerdeT::serialize(ret, RPCError::kUnknown);
        }

        return ret;
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

  private:
    // (logically) immutable resources
    zmq::context_t ctx_{};
    // socket for RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::rep};
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

template <typename SerdeT = Serde>
class Client {
  public:
    using Event = std::string;
    using EventHandler = std::function<bool(Event&)>;   // return bool to delete event?
    using EventQueue = std::map<Event, EventHandler>;

    using AsyncCallbackArgs = zmq::message_t&;
    using AsyncToken = std::string;
    using AsyncHandler = std::function<void(AsyncCallbackArgs)>;
    using AsyncQueue = std::unordered_map<AsyncToken, AsyncHandler>;

    Client(const std::string& endpoint = kEndpoint)
    {
        // todo: set metadata
        sock_.connect(endpoint);
        async_sub_.set(zmq::sockopt::subscribe, "async");
        event_sub_.set(zmq::sockopt::subscribe, "event");

        poll_thread_ = std::thread(&Client::poll_thread, this);

        spdlog::info("cli connect to {}", kEndpoint);
    }

    Client(Client&) = delete;

    ~Client()
    {
        stop_ = true;

        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
    }

    // Calling convention:
    //   - send: [method, args...]
    //   - recv: [error_code, return value]
    // Requires:
    //   - `ReturnType`: is_serializable_type && (is_default_constructible or is_void)
    //   - `Args...`: is_serializable_type
    // Usage:
    //   - must specify return type via `call<int>()` / `call<std::string>()` etc.
    template <typename ReturnType = void, typename... Args>
    auto call(const char* method, Args... args) noexcept(false) -> ReturnType
    {
        zmq::message_t req, resp;

        {
            auto ec = SerdeT::serialize(req, std::string(method), args...);
            auto req_result = sock_.send(req, zmq::send_flags::none);
        }

        {
            auto resp_result = sock_.recv(resp, zmq::recv_flags::none);
            if constexpr (std::is_void_v<ReturnType>) {
                RPCError code;
                auto ec = SerdeT::deserialize(resp, code);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                return;
            } else {
                static_assert(std::is_constructible_v<ReturnType>);
                RPCError code;
                ReturnType ret{};
                auto ec = SerdeT::deserialize(resp, code, ret);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                return ret;
            }
        }
    }

    // Calling convention:
    //   - send: [method, async_token, args...]
    //     - `async_token` is a uuid generated by client, which would be asynchronously emitted
    //       back as [async_token, callback args...] after the asynchronous work is completed
    //       on the server side, then the `Callback` would be invoked as `cb(callback args)`
    //   - recv: [error_code, return value]
    // Thread safety:
    //   - the `Callback` would be invoked in another thread
    template <typename ReturnType = void, typename Callback, typename... Args>
    auto async_call(const char* method, Callback cb, Args... args)
    {
        zmq::message_t req, resp;
        AsyncToken token = generate_token();
        {
            // [method, token, args...]
            auto ec = Serde::serialize(req, std::string(method), token, args...);
            // synchronous send
            auto req_result = sock_.send(req, zmq::send_flags::none);
            // [token, callback args...]
            auto handler = [cb = std::move(cb), token = token](AsyncCallbackArgs msg) {
                using TupleType = typename fn_traits<Callback>::tuple_type;

                AsyncToken token_back;
                TupleType args{};

                auto de = [&msg, &token_back](auto&&... xs) {
                    return Serde::deserialize(msg, token_back, xs...);
                };

                // deserialize args
                auto ec = std::apply(de, args);

                // check token
                assert(token_back == token);

                // invoke the callback
                std::apply(cb, args);
            };

            {
                std::lock_guard<std::mutex> lock{async_q_lock_};
                async_q_.insert({token, std::move(handler)});
            }
        }

        {
            auto resp_result = sock_.recv(resp, zmq::recv_flags::none);

            if constexpr (std::is_void_v<ReturnType>) {
                RPCError code;
                auto ec = SerdeT::deserialize(resp, code);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                return;
            } else {
                static_assert(std::is_constructible_v<ReturnType>);
                RPCError code;
                ReturnType ret{};
                auto ec = SerdeT::deserialize(resp, code, ret);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                return ret;
            }
        }
    }

    // register an event, thread safety?
    inline auto register_event(const Event& event, EventHandler handler)
    {
        std::lock_guard<std::mutex> lock{event_q_lock_};

        event_q_[event] = std::move(handler);
    }

  private:
    // thread for handling async results and server events
    void poll_thread()
    {
        using namespace std::chrono_literals;
        zmq::poller_t<> poller;
        std::vector<zmq::poller_event<>> in_events;

        poller.add(async_sub_, zmq::event_flags::pollin);
        poller.add(event_sub_, zmq::event_flags::pollin);

        while (!stop_) {
            size_t n = poller.wait_all(in_events, -1ms);

            for (size_t i = 0; i < n; i++) {
                zmq::message_t msg;

                auto sock = in_events[i].socket;

                auto recv_result = sock.recv(msg, zmq::recv_flags::none);

                if (sock == async_sub_) {
                    handle_async(msg);
                }

                if (sock == event_sub_) {
                    handle_event(msg);
                }
            }
        }
        spdlog::trace("poll thread stopped normally");
    }

    void handle_async(zmq::message_t& msg)
    {
        AsyncToken token;

        auto ec = Serde::deserialize(msg, token);

        std::lock_guard<std::mutex> lock{async_q_lock_};

        auto handler = async_q_.at(token);

        handler(msg);   // recursive async call? deadlock?

        async_q_.erase(token);
    }

    void handle_event(zmq::message_t& msg)
    {
        Event event;

        auto ec = Serde::deserialize(msg, event);

        std::lock_guard<std::mutex> lock{event_q_lock_};

        if (!event_q_.count(event)) {
            return;
        }

        auto handler = event_q_.at(event);

        bool unregister = !handler(event);

        if (unregister) {
            event_q_.erase(event);
        }
    }

    // thread unsafety
    AsyncToken generate_token()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        int i;
        ss << std::hex;
        for (i = 0; i < 8; i++) {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 4; i++) {
            ss << dis(gen);
        }
        ss << "-4";
        for (i = 0; i < 3; i++) {
            ss << dis(gen);
        }
        ss << "-";
        ss << dis2(gen);
        for (i = 0; i < 3; i++) {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 12; i++) {
            ss << dis(gen);
        };
        return ss.str();
    }

  private:
    // (logically) immutable resources
    zmq::context_t ctx_{};
    // socket for sync RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::req};
    // socket for async RPC calls
    zmq::socket_t async_sub_{ctx_, zmq::socket_type::sub};
    // socket for subscribing events
    zmq::socket_t event_sub_{ctx_, zmq::socket_type::sub};
    // poll thread
    std::thread poll_thread_;

    // mutable states
    std::atomic<bool> stop_{false};

    std::mutex event_q_lock_{};
    EventQueue event_q_{};

    std::mutex async_q_lock_{};
    AsyncQueue async_q_{};
};

}   // namespace zrpc
#endif   // #ifndef _ZRPC_HPP_
