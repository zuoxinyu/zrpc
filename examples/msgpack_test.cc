#include <fmt/core.h>
#include <limits>

#include <msgpack.hpp>

template <typename T>
void TEST_TYPE(T val)
{
    msgpack::Packer packer;
    packer.process(val);
    const auto& vec = packer.vector();
    msgpack::Unpacker unpacker(vec.data(), vec.size());
    T des;
    unpacker.process(des);

    fmt::println("expecting value {} of type: {}, got {}", val, typeid(T).name(), des);
    // assert(val == des);
}

int main()
{
    TEST_TYPE<float>(2.0f);
    TEST_TYPE<float>(-2.0f);
    TEST_TYPE<float>(-1.0f);
    TEST_TYPE<float>(-0.0f);
    TEST_TYPE<float>(+0.0f);
    TEST_TYPE<float>(+0.1234f);
    TEST_TYPE<float>(-0.1234f);
    TEST_TYPE<float>(+1234.5678f);
    TEST_TYPE<float>(-1234.5678f);
    TEST_TYPE<float>(std::numeric_limits<float>::infinity());
    TEST_TYPE<float>(std::numeric_limits<float>::max());
    TEST_TYPE<float>(std::numeric_limits<float>::min());
    TEST_TYPE<float>(std::numeric_limits<float>::denorm_min());

    TEST_TYPE<double>(2.0);
    TEST_TYPE<double>(-2.0);
    TEST_TYPE<double>(-1.0);
    TEST_TYPE<double>(-0.0);
    TEST_TYPE<double>(+0.0);
    TEST_TYPE<double>(+0.1234);
    TEST_TYPE<double>(-0.1234);
    TEST_TYPE<double>(+1234.5678);
    TEST_TYPE<double>(-1234.5678);
    TEST_TYPE<double>(std::numeric_limits<double>::infinity());
    TEST_TYPE<double>(std::numeric_limits<double>::max());
    TEST_TYPE<double>(std::numeric_limits<double>::min());
    TEST_TYPE<double>(std::numeric_limits<double>::denorm_min());
}
