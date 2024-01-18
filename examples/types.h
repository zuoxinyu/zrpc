#include "macros.hpp"

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
DERIVE_ZRPC_STRUCT(StructType)
