#include "uasm/value.h"

#include <cstdlib>

namespace uasm {

namespace {
bool isFloatType(Type::Value t) { return t == Type::F32 || t == Type::F64; }

std::size_t minSize(std::size_t a, std::size_t b) { return a < b ? a : b; }
}

double Value::asDouble() const {
    switch (type) {
        case Type::I8: return static_cast<double>(bits.i8);
        case Type::U8: return static_cast<double>(bits.u8);
        case Type::I16: return static_cast<double>(bits.i16);
        case Type::U16: return static_cast<double>(bits.u16);
        case Type::I32: return static_cast<double>(bits.i32);
        case Type::U32: return static_cast<double>(bits.u32);
        case Type::I64: return static_cast<double>(bits.i64);
        case Type::U64: return static_cast<double>(bits.u64);
        case Type::I128: return static_cast<double>(bits.i128);
        case Type::U128: return static_cast<double>(bits.u128);
        case Type::F32: return static_cast<double>(bits.f32);
        case Type::F64: return bits.f64;
        case Type::Ptr: return static_cast<double>(bits.ptr);
        case Type::Void: return 0.0;
    }
    return 0.0;
}

Int128 Value::asInt128() const {
    switch (type) {
        case Type::I8: return bits.i8;
        case Type::U8: return bits.u8;
        case Type::I16: return bits.i16;
        case Type::U16: return bits.u16;
        case Type::I32: return bits.i32;
        case Type::U32: return bits.u32;
        case Type::I64: return bits.i64;
        case Type::U64: return static_cast<Int128>(bits.u64);
        case Type::I128: return bits.i128;
        case Type::U128: return static_cast<Int128>(bits.u128);
        case Type::F32: return static_cast<Int128>(bits.f32);
        case Type::F64: return static_cast<Int128>(bits.f64);
        case Type::Ptr: return static_cast<Int128>(bits.ptr);
        case Type::Void: return 0;
    }
    return 0;
}

Value Value::fromDouble(Type::Value t, double v) {
    Value r;
    r.type = t;
    switch (t) {
        case Type::I8: r.bits.i8 = static_cast<int8_t>(v); break;
        case Type::U8: r.bits.u8 = static_cast<uint8_t>(v); break;
        case Type::I16: r.bits.i16 = static_cast<int16_t>(v); break;
        case Type::U16: r.bits.u16 = static_cast<uint16_t>(v); break;
        case Type::I32: r.bits.i32 = static_cast<int32_t>(v); break;
        case Type::U32: r.bits.u32 = static_cast<uint32_t>(v); break;
        case Type::I64: r.bits.i64 = static_cast<int64_t>(v); break;
        case Type::U64: r.bits.u64 = static_cast<uint64_t>(v); break;
        case Type::I128: r.bits.i128 = static_cast<Int128>(v); break;
        case Type::U128: r.bits.u128 = static_cast<UInt128>(v); break;
        case Type::F32: r.bits.f32 = static_cast<float>(v); break;
        case Type::F64: r.bits.f64 = v; break;
        case Type::Ptr: r.bits.ptr = static_cast<uint64_t>(v); break;
        case Type::Void: break;
    }
    return r;
}

Value Value::fromInt128(Type::Value t, Int128 v) {
    Value r;
    r.type = t;
    switch (t) {
        case Type::I8: r.bits.i8 = static_cast<int8_t>(v); break;
        case Type::U8: r.bits.u8 = static_cast<uint8_t>(v); break;
        case Type::I16: r.bits.i16 = static_cast<int16_t>(v); break;
        case Type::U16: r.bits.u16 = static_cast<uint16_t>(v); break;
        case Type::I32: r.bits.i32 = static_cast<int32_t>(v); break;
        case Type::U32: r.bits.u32 = static_cast<uint32_t>(v); break;
        case Type::I64: r.bits.i64 = static_cast<int64_t>(v); break;
        case Type::U64: r.bits.u64 = static_cast<uint64_t>(v); break;
        case Type::I128: r.bits.i128 = v; break;
        case Type::U128: r.bits.u128 = static_cast<UInt128>(v); break;
        case Type::F32: r.bits.f32 = static_cast<float>(v); break;
        case Type::F64: r.bits.f64 = static_cast<double>(v); break;
        case Type::Ptr: r.bits.ptr = static_cast<uint64_t>(v); break;
        case Type::Void: break;
    }
    return r;
}

Value bitcast(Type::Value destType, const Value& src) {
    Value out;
    out.type = destType;
    std::size_t n = minSize(sizeOfType(src.type), sizeOfType(destType));
    std::memcpy(&out.bits, &src.bits, n);
    return out;
}

Value parseTypedValue(const std::string& spec) {
    std::string::size_type colon = spec.find(':');
    if (colon == std::string::npos) {
        throw InvalidTypedValue("argument '" + spec + "' is not TYPE:VALUE (e.g. 'i32:10')");
    }
    std::string typeName = spec.substr(0, colon);
    std::string valueStr = spec.substr(colon + 1);

    Type::Value type;
    if (!typeFromName(typeName, type)) {
        throw InvalidTypedValue("unknown type '" + typeName + "' in argument '" + spec + "'");
    }
    if (type == Type::Void) {
        throw InvalidTypedValue("'void' is not a valid argument type in '" + spec + "'");
    }
    if (valueStr.empty()) {
        throw InvalidTypedValue("argument '" + spec + "' is missing a value after ':'");
    }

    const char* begin = valueStr.c_str();
    char* end = 0;
    if (isFloatType(type)) {
        double v = std::strtod(begin, &end);
        if (end != begin + valueStr.size()) {
            throw InvalidTypedValue("value '" + valueStr + "' is not a valid " + typeName + " in argument '" + spec + "'");
        }
        return Value::fromDouble(type, v);
    }

    int64_t v = static_cast<int64_t>(std::strtoll(begin, &end, 0));
    if (end != begin + valueStr.size()) {
        throw InvalidTypedValue("value '" + valueStr + "' is not a valid " + typeName + " in argument '" + spec + "'");
    }
    return Value::fromInt128(type, static_cast<Int128>(v));
}

}
