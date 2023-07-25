#include <fmt/format.h>
#include <magic_enum.hpp>

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
template <>
struct fmt::formatter<StructType> {
    constexpr auto parse(format_parse_context ctx) { return ctx.begin(); }
    auto format(const StructType& st, format_context& ctx) const
    {
        return fmt::format_to(ctx.out(), "StructType{{error: {}, msg: \"{}\"}}", st.error, st.msg);
    }
};

template <>
inline void msgpack::Packer::pack_type<StructType>(const StructType& e)
{
    pack_type(e.error);
    pack_type(e.msg);
}

template <>
inline void msgpack::Unpacker::unpack_type<StructType>(StructType& e)
{
    unpack_type(e.error);
    unpack_type(e.msg);
}
