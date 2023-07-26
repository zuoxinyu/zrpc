#ifndef __ZRPC_TRAITS_HPP__
#define __ZRPC_TRAITS_HPP__

#include <type_traits>
#include <utility>

namespace zrpc::detail {
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

template <typename Tp>
struct tp_traits;

template <typename Car, typename... Cdr>
struct tp_traits<std::tuple<Car, Cdr...>> {
    using car = Car;
    using cdr = std::tuple<Cdr...>;
};

template <typename T>
constexpr inline bool is_pointer_type =
    std::is_pointer_v<remove_cvref_t<T>> || std::is_reference_v<T>;

template <typename T>
struct any_pointer_type {
    static constexpr inline bool value = is_pointer_type<T>;
};

// bypass bug of vs17
template <typename... Args>
constexpr auto any_pointer_type_f()
{
    return (is_pointer_type<Args> || ...);
}

template <typename... Args>
struct any_pointer_type<std::tuple<Args...>> {
    static constexpr inline bool value = any_pointer_type_f<Args...>();
};

template <typename Enum>
constexpr decltype(auto) to_underlying_if_enum(Enum e)
{
    if constexpr (std::is_enum_v<Enum>) {
        return static_cast<typename std::underlying_type<Enum>::type>(e);
    } else {
        return e;
    }
}

static_assert(any_pointer_type<std::tuple<int*, float>>::value);
static_assert(!any_pointer_type<std::tuple<int, float>>::value);

// TODO: whether can be serialized/deserialized to msgpack
template <typename T>
struct is_serializable_type {
    static constexpr bool value = !std::is_pointer_v<T> && !std::is_reference_v<T>;
};

template <typename Fn>
constexpr inline bool is_registerable =
    is_serializable_type<typename fn_traits<Fn>::return_type>::value &&
    !any_pointer_type<typename fn_traits<Fn>::tuple_type>::value;

}   // namespace zrpc::detail

#endif
