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
    Ptr
};
}

const char* const kTypeNames[] = {
    "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64",
    "i128", "u128", "f32", "f64", "void", "ptr",
};

inline const char* typeName(Type::Value t) {
    return kTypeNames[static_cast<unsigned char>(t)];
}

bool typeFromName(const std::string& name, Type::Value& out);

std::size_t sizeOfType(Type::Value t);

}
