#ifndef __ZRPC_HPP__
#define __ZRPC_HPP__
#include <functional>
#include <map>
#include <type_traits>

#include <fmt/ranges.h>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <zmq.hpp>

#include "msgpack.hpp"
#include "traits.hpp"

namespace zrpc {
enum class RPCErrorCode : uint32_t {
    kNoError = 0,

    kBadPayload,
    kBadMethod,

    kUnknown,
};

class RPCError : public std::exception {
  public:
    RPCError(RPCErrorCode code, std::string what)
        : std::exception()
        , code_(code)
        , what_(std::move(what))
    {}
    const char* what() const noexcept override { return what_.c_str(); }
    RPCErrorCode code() const { return code_; }

  private:
    RPCErrorCode code_;
    std::string what_;
};

struct RPCErrorCategory : public std::error_category {
    inline const char* name() const noexcept override { return "zrpc"; }

    std::string message(int ec) const override
    {
        switch (static_cast<zrpc::RPCErrorCode>(ec)) {
        case RPCErrorCode::kNoError: return "(no error)"; break;
        case RPCErrorCode::kBadPayload: return "bad payload"; break;
        case RPCErrorCode::kBadMethod: return "bad method"; break;
        case RPCErrorCode::kUnknown:
        default: return "(unrecognized error)";
        }
    }
};

const RPCErrorCategory theRPCErrorCategory{};

inline std::error_code make_error_code(zrpc::RPCErrorCode e)
{
    return {static_cast<int>(e), theRPCErrorCategory};
}
}   // namespace zrpc

namespace msgpack {
template <>
inline void Packer::pack_type<zrpc::RPCErrorCode>(const zrpc::RPCErrorCode& e)
{
    pack_type(static_cast<const uint32_t>(e));
}
template <>
inline void Unpacker::unpack_type<zrpc::RPCErrorCode>(zrpc::RPCErrorCode& e)
{
    uint32_t u;
    unpack_type(u);
    e = static_cast<zrpc::RPCErrorCode>(u);
}
}   // namespace msgpack

namespace fmt {
template <>
struct formatter<zrpc::RPCErrorCode> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    auto format(const zrpc::RPCErrorCode& ec, format_context& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", magic_enum::enum_name(ec));
    }
};
}   // namespace fmt

namespace std {
template <>
struct is_error_code_enum<zrpc::RPCErrorCode> : public true_type {};
}   // namespace std

namespace zrpc {

using namespace std::chrono_literals;

using Event = std::string;
using EventHandler = std::function<bool(zmq::message_t&)>;   // return bool to delete event?
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

template <typename T, std::enable_if_t<!std::is_enum_v<T>, bool> = true>
static auto process_one(msgpack::Unpacker& unpacker, T& arg)
{
    unpacker.process(arg);
}

template <typename Enum, std::enable_if_t<std::is_enum_v<Enum>, bool> = true>
static auto process_one(msgpack::Unpacker& unpacker, Enum& arg)
{
    using T = typename std::underlying_type<Enum>::type;
    T tmp;
    unpacker.process(tmp);
    arg = static_cast<Enum>(tmp);
}


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
            (process_one(unpacker, args), ...);
            return {};
        } catch (std::error_code ec) {
            return ec;
        }
    }
};

}   // namespace zrpc

#endif   // #ifndef _ZRPC_HPP_
