#include "macros.hpp"
#include "pfr/core_name.hpp"

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

DERIVE_ZRPC_ENUM(EnumType)
DERIVE_ZRPC_ENUM(EnumClass)
DERIVE_ZRPC_STRUCT(Pod)
namespace msgpack {
template <>
inline void Packer ::pack_type<StructType>(const StructType& s)
{
    pfr ::for_each_field(s, [&](const auto& field) { pack_type(field); });
}
template <>
inline void Unpacker ::unpack_type<StructType>(StructType& s)
{
    pfr ::for_each_field(s, [&](auto& field) { unpack_type(field); });
}
}   // namespace msgpack
namespace fmt {
template <>
struct formatter<StructType> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    auto format(const StructType& s, format_context& ctx) const
    {
        fmt ::format_to(ctx.out(), "{}{{", "StructType");
        pfr ::for_each_field(s, [&](const auto& field, size_t idx) {
            fmt ::format_to(ctx.out(), "{}", quoted_if_str(field));
            if (idx < pfr ::tuple_size_v<StructType> - 1) fmt ::format_to(ctx.out(), ", ");
        });
        return fmt ::format_to(ctx.out(), "}}");
    }
};
}   // namespace fmt
