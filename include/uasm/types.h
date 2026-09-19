#pragma once

#include <cstddef>
#include <string>

namespace uasm {

namespace Type {
enum Value {
    I8 = 0,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    I128,
    U128,
    F32,
    F64,
    Void,
    Ptr,
    V128,
    V256,
    V512
};
}

const char* const kTypeNames[] = {
    "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64",
    "i128", "u128", "f32", "f64", "void", "ptr",
    "v128", "v256", "v512",
};

inline const char* typeName(Type::Value t) {
    return kTypeNames[static_cast<unsigned char>(t)];
}

bool typeFromName(const std::string& name, Type::Value& out);

std::size_t sizeOfType(Type::Value t);

inline bool isVectorType(Type::Value t) { return t == Type::V128 || t == Type::V256 || t == Type::V512; }

inline std::size_t vectorBytes(Type::Value t) {
    if (t == Type::V128) return 16;
    if (t == Type::V256) return 32;
    if (t == Type::V512) return 64;
    return 0;
}

}
