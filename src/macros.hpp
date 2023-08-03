#define FIRST_(a, ...) a
#define SECOND_(a, b, ...) b

#define FIRST(...) FIRST_(__VA_ARGS__, )
#define SECOND(...) SECOND_(__VA_ARGS__, )

#define EMPTY()

#define EVAL(...) EVAL1024(__VA_ARGS__)
#define EVAL1024(...) EVAL512(EVAL512(__VA_ARGS__))
#define EVAL512(...) EVAL256(EVAL256(__VA_ARGS__))
#define EVAL256(...) EVAL128(EVAL128(__VA_ARGS__))
#define EVAL128(...) EVAL64(EVAL64(__VA_ARGS__))
#define EVAL64(...) EVAL32(EVAL32(__VA_ARGS__))
#define EVAL32(...) EVAL16(EVAL16(__VA_ARGS__))
#define EVAL16(...) EVAL8(EVAL8(__VA_ARGS__))
#define EVAL8(...) EVAL4(EVAL4(__VA_ARGS__))
#define EVAL4(...) EVAL2(EVAL2(__VA_ARGS__))
#define EVAL2(...) EVAL1(EVAL1(__VA_ARGS__))
#define EVAL1(...) __VA_ARGS__

#define DEFER1(m) m EMPTY()
#define DEFER2(m) m EMPTY EMPTY()()

#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1

#define CAT(a, b) a##b

#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()

#define BOOL(x) NOT(NOT(x))

#define IF_ELSE(condition) _IF_ELSE(BOOL(condition))
#define _IF_ELSE(condition) CAT(_IF_, condition)

#define _IF_1(...) __VA_ARGS__ _IF_1_ELSE
#define _IF_0(...) _IF_0_ELSE

#define _IF_1_ELSE(...)
#define _IF_0_ELSE(...) __VA_ARGS__

#define HAS_ARGS(...) BOOL(FIRST(_END_OF_ARGUMENTS_ __VA_ARGS__)())
#define _END_OF_ARGUMENTS_() 0

#define MAP(m, first, ...)                                               \
    m(first) IF_ELSE(HAS_ARGS(__VA_ARGS__))(                             \
        DEFER2(_MAP)()(m, __VA_ARGS__))(/* Do nothing, just terminate */ \
    )
#define _MAP() MAP

#define DERIVE_FORMATTABLE_ENUM(enum_type)                                     \
    namespace fmt {                                                            \
    template <>                                                                \
    struct formatter<enum_type> {                                              \
        constexpr auto parse(format_parse_context& ctx)                        \
        {                                                                      \
            return ctx.begin();                                                \
        }                                                                      \
        auto format(const enum_type& ec, format_context& ctx) const            \
        {                                                                      \
            return fmt::format_to(ctx.out(), "{}", magic_enum::enum_name(ec)); \
        }                                                                      \
    };                                                                         \
    }

#define FIELDOF(x) s.x,
#define FIELDFMT(x) #x ": {}, "

#define DERIVE_FORMATTABLE_STRUCT(structt, ...)                                         \
    namespace fmt {                                                                     \
    template <>                                                                         \
    struct formatter<structt> {                                                         \
        constexpr auto parse(format_parse_context& ctx)                                 \
        {                                                                               \
            return ctx.begin();                                                         \
        }                                                                               \
        auto format(const structt& s, format_context& ctx) const                        \
        {                                                                               \
            return fmt::format_to(ctx.out(),                                            \
                                  #structt "{{ " EVAL(MAP(FIELDFMT, __VA_ARGS__)) "}}", \
                                  EVAL(MAP(FIELDOF, __VA_ARGS__)));                     \
        }                                                                               \
    };                                                                                  \
    }


#define DERIVE_PACKABLE_STRUCT(pod_type)                                \
    namespace msgpack {                                                 \
    template <>                                                         \
    inline void Packer::pack_type<pod_type>(const pod_type& s)          \
    {                                                                   \
        constexpr size_t sz = sizeof(pod_type);                         \
        std::array<uint8_t, sz> vec;                                    \
        std::memcpy(&vec[0], reinterpret_cast<const uint8_t*>(&s), sz); \
        pack_type(vec);                                                 \
    }                                                                   \
                                                                        \
    template <>                                                         \
    inline void Unpacker::unpack_type<pod_type>(pod_type & s)           \
    {                                                                   \
        constexpr size_t sz = sizeof(pod_type);                         \
        std::array<uint8_t, sz> vec;                                    \
        unpack_type(vec);                                               \
        std::memcpy(reinterpret_cast<uint8_t*>(&s), &vec[0], sz);       \
    }                                                                   \
    }

// #define apply_(x) f(x);
// #define apply(...) EVAL(MAP(apply_, __VA_ARGS__))
