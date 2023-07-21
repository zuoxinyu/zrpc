#ifndef __ZRPC_HPP__
#define __ZRPC_HPP__
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <zmq.hpp>

#include "msgpack.hpp"
#include "traits.hpp"

namespace zrpc {
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

using namespace std::chrono_literals;

using Event = std::string;
using EventHandler = std::function<bool(Event&)>;   // return bool to delete event?
using EventQueue = std::map<Event, EventHandler>;

using AsyncCallbackArgs = zmq::message_t&;
using AsyncToken = std::string;
using AsyncHandler = std::function<void(AsyncCallbackArgs)>;
using AsyncQueue = std::unordered_map<AsyncToken, AsyncHandler>;

static const std::string kEndpoint = "tcp://127.0.0.1:5555";
static const std::string kAsyncEndpoint = "tcp://127.0.0.1:5556";
static const std::string kEventEndpoint = "tcp://127.0.0.1:5557";
static const std::string kAsyncFilter = "";   // FIXME: figure out this strange usage...
static const std::string kEventFilter = "";
static const std::string kHandshake = "hello";
static const std::string kHandshakeReply = "hi";

// default [De]serialize implementation
// TODO: make it a custom point in a better way
struct Serde {
    template <typename... Args>
    [[nodiscard]] static auto serialize(zmq::message_t& msg, const Args&... args)
        -> std::error_code   // TODO: exception instead?
    {
        try {
            msgpack::Packer packer;
            packer.process(detail::to_underlying_if_enum(args)...);
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

            auto process = [&](auto& arg) {
                using T = std::remove_reference_t<decltype(arg)>;
                if constexpr (std::is_enum_v<T>) {
                    typename std::underlying_type<T>::type tmp;
                    unpacker.process(tmp);
                    arg = static_cast<T>(tmp);
                } else {
                    unpacker.process(arg);
                }
            };
            (process(args), ...);
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }
};

}   // namespace zrpc
#endif   // #ifndef _ZRPC_HPP_
