#ifndef __ZRPC_CLIENT_HPP__
#define __ZRPC_CLIENT_HPP__

#include "zrpc.hpp"

namespace zrpc {

using detail::fn_traits;

template <typename SerdeT = Serde>
class Client {
  public:
    Client(const std::string& endpoint = kEndpoint)
    {
        // todo: set metadata
        // sock_.set(zmq::sockopt::routing_id, identity_);
        async_sub_.set(zmq::sockopt::subscribe, kAsyncFilter);
        event_sub_.set(zmq::sockopt::subscribe, kEventFilter);

        sock_.connect(endpoint);
        async_sub_.connect(kAsyncEndpoint);
        event_sub_.connect(kEventEndpoint);

        // try_handshake();

        // poll_thread_ = std::thread(&Client::poll_thread, this);

        spdlog::info("cli connect to {}", kEndpoint);
    }

    Client(Client&) = delete;

    ~Client()
    {
        // TODO: waiting for pending async tokens?
        // e.g. waiting for `async_q_.empty() == true` ?
        stop_ = true;

        // disconnect from server

        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
    }

    // poll style api
    //   - return value: number of pending async operations
    //   - args: `timeout`
    int poll(std::chrono::milliseconds timeout = -1ms) { return poll_async_sub(timeout); }

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
                spdlog::trace("client call {}{} -> void", method, std::make_tuple(args...));
                return;
            } else {
                static_assert(std::is_constructible_v<ReturnType>);
                RPCError code;
                ReturnType ret{};
                auto ec = SerdeT::deserialize(resp, code, ret);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                spdlog::trace("client call {}{} -> {}", method, std::make_tuple(args...), ret);
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
    //   - the `Callback` would be invoked on another thread
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
                std::string filter;

                auto de = [&](auto&&... xs) {
                    return Serde::deserialize(msg, filter, token_back, xs...);
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
                spdlog::trace(
                    "client async call[{}] {}{} -> void", token, method, std::make_tuple(args...));
                return;
            } else {
                static_assert(std::is_constructible_v<ReturnType>);
                RPCError code;
                ReturnType ret{};
                auto ec = SerdeT::deserialize(resp, code, ret);
                if (code != RPCError::kNoError) {
                    throw code;
                }
                spdlog::trace("client async call[{}] {}{} -> {}",
                              token,
                              method,
                              std::make_tuple(args...),
                              ret);
                return ret;
            }
        }
    }

    // register an event, handler would be invoked on poll thread
    inline auto register_event(const Event& event, EventHandler handler)
    {
        std::lock_guard<std::mutex> lock{event_q_lock_};

        event_q_[event] = std::move(handler);
    }

  private:
    void try_handshake()
    {
        if (!async_sub_connected_) {
            zmq::message_t msg, req, resp;
            std::string handshake;

            std::ignore = async_sub_.recv(msg, zmq::recv_flags::none);
            std::ignore = Serde::deserialize(msg, handshake);
            if (handshake == kHandshake) {
                std::ignore = Serde::serialize(req, kHandshakeReply);
                std::ignore = sock_.send(req, zmq::send_flags::none);
                std::ignore = sock_.recv(resp, zmq::recv_flags::none);
                std::ignore = resp;
                async_sub_connected_ = true;
            }
        }
    }

    int poll_async_sub(std::chrono::milliseconds timeout)
    {
        zmq::message_t msg;
        // FIXME: message lost would occur, need synchronization
        auto recv_result = async_sub_.recv(msg, zmq::recv_flags::none);
        handle_async(msg);

        std::lock_guard<std::mutex> lock{async_q_lock_};
        return async_q_.size();
    }

    // thread for handling async results and server events
    void poll_thread()
    {
        using namespace std::chrono_literals;
        zmq::poller_t<> poller;
        std::vector<zmq::poller_event<>> in_events;

        poller.add(async_sub_, zmq::event_flags::pollin);
        poller.add(event_sub_, zmq::event_flags::pollin);

        while (!stop_) {
            size_t n;
            try {
                n = poller.wait_all(in_events, -1ms);
            } catch (std::exception& e) {
                spdlog::error("zmq::poll: {}", e.what());
            }
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

    // msg: ["async", token, args...]
    void handle_async(zmq::message_t& msg)
    {
        AsyncToken token;
        std::string filter;

        auto ec = Serde::deserialize(msg, filter, token);
        assert(filter == kAsyncFilter);

        std::lock_guard<std::mutex> lock{async_q_lock_};

        if (async_q_.count(token)) {
            auto handler = async_q_.at(token);
            // TODO:
            //   - recursive async call?
            //   - deadlock?
            //   - repeat callback?
            handler(msg);

            // clear completed token
            async_q_.erase(token);
        } else {
            // token from other/obsoleted clients?
            spdlog::warn("unknown async token: [{}]", token);
        }
    }

    void handle_event(zmq::message_t& msg)
    {
        Event event;
        std::string filter;

        auto ec = Serde::deserialize(msg, filter, event);
        assert(filter == kEventFilter);

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
    std::string identity_ = generate_token();
    // (logically) immutable resources
    zmq::context_t ctx_{1};
    // socket for sync RPC calls
    zmq::socket_t sock_{ctx_, zmq::socket_type::req};
    // socket for async RPC calls
    zmq::socket_t async_sub_{ctx_, zmq::socket_type::sub};
    // socket for subscribing events TODO: use one subscriber
    zmq::socket_t event_sub_{ctx_, zmq::socket_type::sub};
    // poll thread
    std::thread poll_thread_;

    // mutable states
    std::atomic<bool> stop_{false};

    // registered events
    std::mutex event_q_lock_{};
    EventQueue event_q_{};

    // waiting async operations
    std::mutex async_q_lock_{};
    AsyncQueue async_q_{};
    std::atomic<bool> async_sub_connected_{false};
};

}   // namespace zrpc
#endif
