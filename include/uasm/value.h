#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "uasm/types.h"

namespace uasm {

#if defined(__SIZEOF_INT128__)
typedef __int128 Int128;
typedef unsigned __int128 UInt128;
#else
#error "uASM requires a compiler with 128-bit integer support (__int128)"
#endif

struct Value {
    Type::Value type;
    union Bits {
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        Int128 i128;
        UInt128 u128;
        float f32;
        double f64;
        uint64_t ptr;
        Bits() : u128(0) {}
    } bits;

    Value() : type(Type::I32) {}

    static Value makeI32(int32_t v) {
        Value r;
        r.type = Type::I32;
        r.bits.i32 = v;
        return r;
    }

    double asDouble() const;

    Int128 asInt128() const;

    static Value fromDouble(Type::Value t, double v);
    static Value fromInt128(Type::Value t, Int128 v);
};

Value bitcast(Type::Value destType, const Value& src);

struct InvalidTypedValue : std::invalid_argument {
    explicit InvalidTypedValue(const std::string& why) : std::invalid_argument(why) {}
};

Value parseTypedValue(const std::string& spec);

}
