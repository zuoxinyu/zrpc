#include "pfr.hpp"
#include <fmt/format.h>
#include <magic_enum.hpp>

#include "macros.hpp"
#include "msgpack.hpp"

enum EnumType {
    kState1 = 1,
    kState2 = 2,
    kState3 = 3,
};

enum class EnumClass {
    kStep1,
    kStep2,
    kStep3,
};

struct StructType {
    int error;
    std::string msg;
};

struct Pod {
    int integer;
    uint8_t charactor;
    float floating;
    double double_floating;
};

template <>
struct fmt::formatter<EnumType> {
    constexpr auto parse(format_parse_context ctx) { return ctx.begin(); }
    auto format(const EnumType& ec, format_context& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", magic_enum::enum_name(ec));
    }
};
template <>
struct fmt::formatter<EnumClass> {
    constexpr auto parse(format_parse_context ctx) { return ctx.begin(); }
    auto format(const EnumClass& ec, format_context& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", magic_enum::enum_name(ec));
    }
};

DERIVE_ZRPC_STRUCT(Pod)
DERIVE_ZRPC_STRUCT(StructType)
