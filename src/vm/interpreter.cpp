#include "uasm/interpreter.h"

#include "syscall_platform.h"
#include "vector_ops.h"

#include "uasm/compat.h"
#include "uasm/opcode_info.h"

#include <cmath>
#include <cstring>
#include <sstream>

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif

namespace uasm {

namespace {

#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, size_t> NameIndexMap;
#else
typedef std::map<std::string, size_t> NameIndexMap;
#endif

template <typename T>
std::string toString(T v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

struct Frame {
    std::vector<Value> regs;
    int cmpFlag;
    bool overflowFlag;
    bool carryFlag;
    uint64_t framePointer;
    uint64_t enterSize;
    size_t pc;
    std::vector<uint64_t> allocas;

    Frame() : cmpFlag(0), overflowFlag(false), carryFlag(false), framePointer(0), enterSize(0), pc(0) {}

    Value& reg(uint32_t idx) {
        if (idx >= regs.size()) regs.resize(idx + 1);
        return regs[idx];
    }
};

bool isFloatType(Type::Value t) { return t == Type::F32 || t == Type::F64; }

size_t bitWidth(Type::Value t) { return sizeOfType(t) * 8; }

Value readOperand(Frame& frame, const Operand& op, Type::Value instrType) {
    switch (op.kind) {
        case Operand::Register:
            return frame.reg(op.reg);
        case Operand::ImmediateInt:
            return isFloatType(instrType) ? Value::fromDouble(instrType, static_cast<double>(op.immInt))
                                           : Value::fromInt128(instrType, op.immInt);
        case Operand::ImmediateFloat:
            return Value::fromDouble(instrType, op.immFloat);
        case Operand::Symbol:
        case Operand::Label:
            throw RuntimeError("symbol/label used as a value operand");
    }
    throw RuntimeError("invalid operand");
}

Value arith(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    if (isFloatType(type)) {
        double x = a.asDouble();
        double y = b.asDouble();
        double r = 0;
        switch (op) {
            case Opcode::Add: r = x + y; break;
            case Opcode::Sub: r = x - y; break;
            case Opcode::Mul: r = x * y; break;
            case Opcode::Div:
                if (y == 0.0) throw RuntimeError("division by zero");
                r = x / y;
                break;
            default: throw RuntimeError("not an arithmetic opcode");
        }
        return Value::fromDouble(type, r);
    }
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    Int128 r = 0;
    switch (op) {
        case Opcode::Add: r = x + y; break;
        case Opcode::Sub: r = x - y; break;
        case Opcode::Mul: r = x * y; break;
        case Opcode::Div:
            if (y == 0) throw RuntimeError("division by zero");
            r = x / y;
            break;
        case Opcode::Mod:
            if (y == 0) throw RuntimeError("modulo by zero");
            r = x % y;
            break;
        default: throw RuntimeError("not an arithmetic opcode");
    }
    return Value::fromInt128(type, r);
}

Value bitwise(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    if (isFloatType(type)) throw RuntimeError("bitwise operations are not defined on float types");
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    Int128 r = 0;
    switch (op) {
        case Opcode::And: r = x & y; break;
        case Opcode::Or: r = x | y; break;
        case Opcode::Xor: r = x ^ y; break;
        default: throw RuntimeError("not a bitwise opcode");
    }
    return Value::fromInt128(type, r);
}

Value bitwiseNot(Type::Value type, const Value& a) {
    if (isFloatType(type)) throw RuntimeError("bitwise operations are not defined on float types");
    return Value::fromInt128(type, ~a.asInt128());
}

Value shift(Opcode::Value op, Type::Value type, const Value& a, const Value& amount) {
    if (isFloatType(type)) throw RuntimeError("shifts are not defined on float types");
    Int128 x = a.asInt128();
    Int128 n = amount.asInt128();
    if (n < 0 || n >= static_cast<Int128>(sizeOfType(type) * 8)) {
        throw RuntimeError("shift amount out of range");
    }
    Int128 r = (op == Opcode::Shl) ? (x << static_cast<int>(n)) : (x >> static_cast<int>(n));
    return Value::fromInt128(type, r);
}

Value negate(Type::Value type, const Value& a) {
    if (isFloatType(type)) return Value::fromDouble(type, -a.asDouble());
    return Value::fromInt128(type, -a.asInt128());
}

int compare(Type::Value type, const Value& a, const Value& b) {
    if (isFloatType(type)) {
        double x = a.asDouble();
        double y = b.asDouble();
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
    }
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

Value absValue(Type::Value type, const Value& a) {
    if (isFloatType(type)) return Value::fromDouble(type, std::fabs(a.asDouble()));
    Int128 x = a.asInt128();
    return Value::fromInt128(type, x < 0 ? -x : x);
}

Value minMaxValue(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    if (isFloatType(type)) {
        double x = a.asDouble();
        double y = b.asDouble();
        bool aLess = x < y;
        return Value::fromDouble(type, (op == Opcode::Min) == aLess ? x : y);
    }
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    bool aLess = x < y;
    return Value::fromInt128(type, (op == Opcode::Min) == aLess ? x : y);
}

Value floatUnary(Opcode::Value op, Type::Value type, const Value& a) {
    if (!isFloatType(type)) throw RuntimeError("this operation requires a float type (f32/f64)");
    double x = a.asDouble();
    double r = 0;
    switch (op) {
        case Opcode::Sqrt: r = std::sqrt(x); break;
        case Opcode::Cbrt: r = std::cbrt(x); break;
        case Opcode::Floor: r = std::floor(x); break;
        case Opcode::Ceil: r = std::ceil(x); break;
        case Opcode::Round: r = std::floor(x + 0.5); break;
        case Opcode::Trunc: r = std::trunc(x); break;
        case Opcode::Sin: r = std::sin(x); break;
        case Opcode::Cos: r = std::cos(x); break;
        case Opcode::Tan: r = std::tan(x); break;
        case Opcode::Asin: r = std::asin(x); break;
        case Opcode::Acos: r = std::acos(x); break;
        case Opcode::Atan: r = std::atan(x); break;
        case Opcode::Sinh: r = std::sinh(x); break;
        case Opcode::Cosh: r = std::cosh(x); break;
        case Opcode::Tanh: r = std::tanh(x); break;
        case Opcode::Log: r = std::log(x); break;
        case Opcode::Log2: r = std::log(x) / std::log(2.0); break;
        case Opcode::Log10: r = std::log10(x); break;
        case Opcode::Exp: r = std::exp(x); break;
        case Opcode::Exp2: r = std::pow(2.0, x); break;
        default: throw RuntimeError("not a unary math opcode");
    }
    return Value::fromDouble(type, r);
}

Value floatBinary(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    if (!isFloatType(type)) throw RuntimeError("this operation requires a float type (f32/f64)");
    double x = a.asDouble();
    double y = b.asDouble();
    double r = 0;
    switch (op) {
        case Opcode::Pow: r = std::pow(x, y); break;
        case Opcode::Atan2: r = std::atan2(x, y); break;
        case Opcode::Hypot: r = std::sqrt(x * x + y * y); break;
        case Opcode::Copysign: r = (y < 0.0) ? -std::fabs(x) : std::fabs(x); break;
        case Opcode::Fmod: r = std::fmod(x, y); break;
        default: throw RuntimeError("not a binary math opcode");
    }
    return Value::fromDouble(type, r);
}

Value fmaValue(Type::Value type, const Value& a, const Value& b, const Value& c) {
    if (!isFloatType(type)) throw RuntimeError("fma requires a float type (f32/f64)");
    return Value::fromDouble(type, a.asDouble() * b.asDouble() + c.asDouble());
}

UInt128 maskToWidth(UInt128 v, size_t bits) {
    if (bits >= 128) return v;
    return v & ((static_cast<UInt128>(1) << bits) - 1);
}

int popcountGeneric(UInt128 v, size_t bits) {
    int c = 0;
    for (size_t i = 0; i < bits; ++i) {
        if ((v >> i) & 1) ++c;
    }
    return c;
}

int clzGeneric(UInt128 v, size_t bits) {
    for (size_t i = bits; i-- > 0;) {
        if ((v >> i) & 1) return static_cast<int>(bits - 1 - i);
    }
    return static_cast<int>(bits);
}

int ctzGeneric(UInt128 v, size_t bits) {
    for (size_t i = 0; i < bits; ++i) {
        if ((v >> i) & 1) return static_cast<int>(i);
    }
    return static_cast<int>(bits);
}

UInt128 bitreverseGeneric(UInt128 v, size_t bits) {
    UInt128 r = 0;
    for (size_t i = 0; i < bits; ++i) {
        r <<= 1;
        r |= (v & 1);
        v >>= 1;
    }
    return r;
}

UInt128 bswapGeneric(UInt128 v, size_t bytes) {
    UInt128 r = 0;
    for (size_t i = 0; i < bytes; ++i) {
        r = (r << 8) | ((v >> (i * 8)) & 0xFF);
    }
    return r;
}

UInt128 rotlGeneric(UInt128 v, size_t bits, size_t amount) {
    if (amount == 0) return v;
    return (v << amount) | (v >> (bits - amount));
}

UInt128 rotrGeneric(UInt128 v, size_t bits, size_t amount) {
    if (amount == 0) return v;
    return (v >> amount) | (v << (bits - amount));
}

Value bitUnary(Opcode::Value op, Type::Value type, const Value& a) {
    if (isFloatType(type)) throw RuntimeError("bit manipulation is not defined on float types");
    size_t bits = bitWidth(type);
    UInt128 v = maskToWidth(static_cast<UInt128>(a.asInt128()), bits);
    switch (op) {
        case Opcode::Popcount: return Value::fromInt128(type, popcountGeneric(v, bits));
        case Opcode::Clz: return Value::fromInt128(type, clzGeneric(v, bits));
        case Opcode::Ctz: return Value::fromInt128(type, ctzGeneric(v, bits));
        case Opcode::Parity: return Value::fromInt128(type, popcountGeneric(v, bits) & 1);
        case Opcode::Ffs: return Value::fromInt128(type, v == 0 ? 0 : ctzGeneric(v, bits) + 1);
        case Opcode::Bitreverse: return Value::fromInt128(type, static_cast<Int128>(bitreverseGeneric(v, bits)));
        case Opcode::Bswap: return Value::fromInt128(type, static_cast<Int128>(bswapGeneric(v, sizeOfType(type))));
        default: throw RuntimeError("not a unary bit-manipulation opcode");
    }
}

Value rotateValue(Opcode::Value op, Type::Value type, const Value& a, const Value& amount) {
    if (isFloatType(type)) throw RuntimeError("bit manipulation is not defined on float types");
    size_t bits = bitWidth(type);
    Int128 n = amount.asInt128();
    if (n < 0 || static_cast<size_t>(n) >= bits) throw RuntimeError("rotate amount out of range");
    UInt128 v = maskToWidth(static_cast<UInt128>(a.asInt128()), bits);
    UInt128 r = (op == Opcode::Rotl) ? rotlGeneric(v, bits, static_cast<size_t>(n))
                                      : rotrGeneric(v, bits, static_cast<size_t>(n));
    return Value::fromInt128(type, static_cast<Int128>(r));
}

Value bitIndexOp(Opcode::Value op, Type::Value type, const Value& a, const Value& bitIndex) {
    if (isFloatType(type)) throw RuntimeError("bit manipulation is not defined on float types");
    size_t bits = bitWidth(type);
    Int128 idx = bitIndex.asInt128();
    if (idx < 0 || static_cast<size_t>(idx) >= bits) throw RuntimeError("bit index out of range");
    UInt128 v = maskToWidth(static_cast<UInt128>(a.asInt128()), bits);
    UInt128 mask = static_cast<UInt128>(1) << static_cast<size_t>(idx);
    switch (op) {
        case Opcode::Bitset: return Value::fromInt128(type, static_cast<Int128>(v | mask));
        case Opcode::Bitclear: return Value::fromInt128(type, static_cast<Int128>(v & ~mask));
        case Opcode::Bittest: return Value::fromInt128(type, (v & mask) != 0 ? 1 : 0);
        default: throw RuntimeError("not a bit-index opcode");
    }
}

Int128 signedMinOfWidth(size_t bits) {
    if (bits >= 128) return static_cast<Int128>(static_cast<UInt128>(1) << 127);
    return -(static_cast<Int128>(1) << (bits - 1));
}

Int128 signedMaxOfWidth(size_t bits) {
    if (bits >= 128) return static_cast<Int128>((~static_cast<UInt128>(0)) >> 1);
    return (static_cast<Int128>(1) << (bits - 1)) - 1;
}

UInt128 unsignedMaxOfWidth(size_t bits) {
    if (bits >= 128) return ~static_cast<UInt128>(0);
    return (static_cast<UInt128>(1) << bits) - 1;
}

UInt128 unsignedOf(const Value& v, Type::Value type) {
    return maskToWidth(static_cast<UInt128>(v.asInt128()), bitWidth(type));
}

void requireInt(Type::Value type, const char* what) {
    if (isFloatType(type)) throw RuntimeError(std::string(what) + " requires an integer type");
}

Int128 ipow(Int128 base, Int128 exp) {
    if (exp < 0) throw RuntimeError("powi exponent must be non-negative");
    Int128 r = 1;
    while (exp > 0) {
        if (exp & 1) r *= base;
        base *= base;
        exp >>= 1;
    }
    return r;
}

UInt128 uisqrt(UInt128 n) {
    if (n == 0) return 0;
    UInt128 x = n;
    UInt128 y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

int ulog2(UInt128 v) {
    int r = -1;
    while (v > 0) {
        ++r;
        v >>= 1;
    }
    return r;
}

int ulog10(UInt128 v) {
    int r = -1;
    while (v > 0) {
        ++r;
        v /= 10;
    }
    return r;
}

UInt128 nextPow2(UInt128 v) {
    if (v <= 1) return 1;
    UInt128 r = 1;
    while (r < v) r <<= 1;
    return r;
}

UInt128 prevPow2(UInt128 v) {
    if (v == 0) return 0;
    UInt128 r = 1;
    while ((r << 1) != 0 && (r << 1) <= v) r <<= 1;
    return r;
}

Int128 floorDiv(Int128 a, Int128 b) {
    if (b == 0) throw RuntimeError("division by zero");
    Int128 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

Int128 ceilDiv(Int128 a, Int128 b) {
    if (b == 0) throw RuntimeError("division by zero");
    Int128 q = a / b;
    if ((a % b != 0) && ((a < 0) == (b < 0))) ++q;
    return q;
}

Int128 euclidDiv(Int128 a, Int128 b) {
    if (b == 0) throw RuntimeError("division by zero");
    Int128 q = a / b;
    if (a % b < 0) q += (b > 0) ? -1 : 1;
    return q;
}

Int128 euclidMod(Int128 a, Int128 b) {
    if (b == 0) throw RuntimeError("modulo by zero");
    Int128 m = a % b;
    if (m < 0) m += (b < 0) ? -b : b;
    return m;
}

Value intUnaryExt(Opcode::Value op, Type::Value type, const Value& a) {
    requireInt(type, "this operation");
    size_t bits = bitWidth(type);
    Int128 x = a.asInt128();
    UInt128 u = unsignedOf(a, type);
    switch (op) {
        case Opcode::Inc: return Value::fromInt128(type, x + 1);
        case Opcode::Dec: return Value::fromInt128(type, x - 1);
        case Opcode::Signum: return Value::fromInt128(type, x > 0 ? 1 : (x < 0 ? -1 : 0));
        case Opcode::Sqr: return Value::fromInt128(type, x * x);
        case Opcode::Isqrt: {
            if (x < 0) throw RuntimeError("isqrt of a negative value");
            return Value::fromInt128(type, static_cast<Int128>(uisqrt(static_cast<UInt128>(x))));
        }
        case Opcode::Ilog2: {
            if (u == 0) throw RuntimeError("ilog2 of zero");
            return Value::fromInt128(type, ulog2(u));
        }
        case Opcode::Ilog10: {
            if (u == 0) throw RuntimeError("ilog10 of zero");
            return Value::fromInt128(type, ulog10(u));
        }
        case Opcode::Ispow2: return Value::fromInt128(type, (u != 0 && (u & (u - 1)) == 0) ? 1 : 0);
        case Opcode::Nextpow2: return Value::fromInt128(type, static_cast<Int128>(nextPow2(u)));
        case Opcode::Prevpow2: return Value::fromInt128(type, static_cast<Int128>(prevPow2(u)));
        case Opcode::Negsat: {
            Int128 lo = signedMinOfWidth(bits);
            if (x == lo) return Value::fromInt128(type, signedMaxOfWidth(bits));
            return Value::fromInt128(type, -x);
        }
        default: throw RuntimeError("not a unary integer opcode");
    }
}

Value intBinaryExt(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    requireInt(type, "this operation");
    size_t bits = bitWidth(type);
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    UInt128 ux = unsignedOf(a, type);
    UInt128 uy = unsignedOf(b, type);
    switch (op) {
        case Opcode::Udiv:
            if (uy == 0) throw RuntimeError("division by zero");
            return Value::fromInt128(type, static_cast<Int128>(ux / uy));
        case Opcode::Umod:
            if (uy == 0) throw RuntimeError("modulo by zero");
            return Value::fromInt128(type, static_cast<Int128>(ux % uy));
        case Opcode::Sar: {
            if (y < 0 || static_cast<size_t>(y) >= bits) throw RuntimeError("shift amount out of range");
            return Value::fromInt128(type, x >> static_cast<int>(y));
        }
        case Opcode::Mulhi: {
            if (bits > 64) throw RuntimeError("mulhi is not defined for 128-bit types");
            return Value::fromInt128(type, (x * y) >> static_cast<int>(bits));
        }
        case Opcode::Umulhi: {
            if (bits > 64) throw RuntimeError("umulhi is not defined for 128-bit types");
            return Value::fromInt128(type, static_cast<Int128>((ux * uy) >> static_cast<int>(bits)));
        }
        case Opcode::Umin: return Value::fromInt128(type, static_cast<Int128>(ux < uy ? ux : uy));
        case Opcode::Umax: return Value::fromInt128(type, static_cast<Int128>(ux > uy ? ux : uy));
        case Opcode::Absdiff: return Value::fromInt128(type, x > y ? x - y : y - x);
        case Opcode::Uabsdiff: return Value::fromInt128(type, static_cast<Int128>(ux > uy ? ux - uy : uy - ux));
        case Opcode::Avg: return Value::fromInt128(type, floorDiv(x + y, 2));
        case Opcode::Uavg: return Value::fromInt128(type, static_cast<Int128>((ux & uy) + ((ux ^ uy) >> 1)));
        case Opcode::Nand: return Value::fromInt128(type, ~(x & y));
        case Opcode::Nor: return Value::fromInt128(type, ~(x | y));
        case Opcode::Xnor: return Value::fromInt128(type, ~(x ^ y));
        case Opcode::Andnot: return Value::fromInt128(type, x & ~y);
        case Opcode::Ornot: return Value::fromInt128(type, x | ~y);
        case Opcode::Powi: return Value::fromInt128(type, ipow(x, y));
        case Opcode::Gcd: {
            UInt128 p = ux, q = uy;
            while (q != 0) {
                UInt128 t = p % q;
                p = q;
                q = t;
            }
            return Value::fromInt128(type, static_cast<Int128>(p));
        }
        case Opcode::Lcm: {
            if (ux == 0 || uy == 0) return Value::fromInt128(type, 0);
            UInt128 p = ux, q = uy;
            while (q != 0) {
                UInt128 t = p % q;
                p = q;
                q = t;
            }
            return Value::fromInt128(type, static_cast<Int128>((ux / p) * uy));
        }
        case Opcode::Divceil: return Value::fromInt128(type, ceilDiv(x, y));
        case Opcode::Divfloor: return Value::fromInt128(type, floorDiv(x, y));
        case Opcode::Diveuclid: return Value::fromInt128(type, euclidDiv(x, y));
        case Opcode::Modeuclid: return Value::fromInt128(type, euclidMod(x, y));
        case Opcode::Alignup: {
            if (uy == 0) throw RuntimeError("alignup to zero");
            return Value::fromInt128(type, static_cast<Int128>(((ux + uy - 1) / uy) * uy));
        }
        case Opcode::Aligndown: {
            if (uy == 0) throw RuntimeError("aligndown to zero");
            return Value::fromInt128(type, static_cast<Int128>((ux / uy) * uy));
        }
        default: throw RuntimeError("not a binary integer opcode");
    }
}

Value saturatingOp(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    requireInt(type, "saturating arithmetic");
    size_t bits = bitWidth(type);
    Int128 lo = signedMinOfWidth(bits);
    Int128 hi = signedMaxOfWidth(bits);
    UInt128 umax = unsignedMaxOfWidth(bits);
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    UInt128 ux = unsignedOf(a, type);
    UInt128 uy = unsignedOf(b, type);

    switch (op) {
        case Opcode::Addsat: {
            Int128 r = x + y;
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return Value::fromInt128(type, r);
        }
        case Opcode::Subsat: {
            Int128 r = x - y;
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return Value::fromInt128(type, r);
        }
        case Opcode::Mulsat: {
            if (bits > 64) throw RuntimeError("mulsat is not defined for 128-bit types");
            Int128 r = x * y;
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return Value::fromInt128(type, r);
        }
        case Opcode::Shlsat: {
            if (y < 0 || static_cast<size_t>(y) >= bits) throw RuntimeError("shift amount out of range");
            Int128 r = x << static_cast<int>(y);
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return Value::fromInt128(type, r);
        }
        case Opcode::Uaddsat: {
            UInt128 r = ux + uy;
            if (r > umax || r < ux) r = umax;
            return Value::fromInt128(type, static_cast<Int128>(r));
        }
        case Opcode::Usubsat: return Value::fromInt128(type, static_cast<Int128>(ux < uy ? 0 : ux - uy));
        case Opcode::Umulsat: {
            if (bits > 64) throw RuntimeError("umulsat is not defined for 128-bit types");
            UInt128 r = ux * uy;
            if (r > umax) r = umax;
            return Value::fromInt128(type, static_cast<Int128>(r));
        }
        default: throw RuntimeError("not a saturating opcode");
    }
}

bool computeWithOverflow(Opcode::Value op, Type::Value type, const Value& a, const Value& b, Int128& result) {
    size_t bits = bitWidth(type);
    Int128 lo = signedMinOfWidth(bits);
    Int128 hi = signedMaxOfWidth(bits);
    UInt128 umax = unsignedMaxOfWidth(bits);
    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    UInt128 ux = unsignedOf(a, type);
    UInt128 uy = unsignedOf(b, type);

    switch (op) {
        case Opcode::Addo:
        case Opcode::Addchk: {
            Int128 r = x + y;
            result = r;
            return r < lo || r > hi;
        }
        case Opcode::Subo:
        case Opcode::Subchk: {
            Int128 r = x - y;
            result = r;
            return r < lo || r > hi;
        }
        case Opcode::Mulo:
        case Opcode::Mulchk: {
            if (bits > 64) throw RuntimeError("checked multiply is not defined for 128-bit types");
            Int128 r = x * y;
            result = r;
            return r < lo || r > hi;
        }
        case Opcode::Shlo: {
            if (y < 0 || static_cast<size_t>(y) >= bits) throw RuntimeError("shift amount out of range");
            Int128 r = x << static_cast<int>(y);
            result = r;
            return r < lo || r > hi;
        }
        case Opcode::Nego: {
            result = -x;
            return x == lo;
        }
        case Opcode::Uaddo:
        case Opcode::Uaddchk: {
            UInt128 r = ux + uy;
            result = static_cast<Int128>(r);
            return r > umax || r < ux;
        }
        case Opcode::Usubo:
        case Opcode::Usubchk: {
            result = static_cast<Int128>(ux - uy);
            return ux < uy;
        }
        case Opcode::Umulo:
        case Opcode::Umulchk: {
            if (bits > 64) throw RuntimeError("checked multiply is not defined for 128-bit types");
            UInt128 r = ux * uy;
            result = static_cast<Int128>(r);
            return r > umax;
        }
        default: throw RuntimeError("not a checked-arithmetic opcode");
    }
}

UInt128 bextGeneric(UInt128 v, UInt128 mask, size_t bits) {
    UInt128 r = 0;
    size_t k = 0;
    for (size_t i = 0; i < bits; ++i) {
        if ((mask >> i) & 1) {
            if ((v >> i) & 1) r |= static_cast<UInt128>(1) << k;
            ++k;
        }
    }
    return r;
}

UInt128 bdepGeneric(UInt128 v, UInt128 mask, size_t bits) {
    UInt128 r = 0;
    size_t k = 0;
    for (size_t i = 0; i < bits; ++i) {
        if ((mask >> i) & 1) {
            if ((v >> k) & 1) r |= static_cast<UInt128>(1) << i;
            ++k;
        }
    }
    return r;
}

UInt128 lowMask(size_t n) {
    if (n == 0) return 0;
    if (n >= 128) return ~static_cast<UInt128>(0);
    return (static_cast<UInt128>(1) << n) - 1;
}

UInt128 interleaveBits(UInt128 v, size_t bits) {
    UInt128 r = 0;
    for (size_t i = 0; i * 2 < bits; ++i) {
        if ((v >> i) & 1) r |= static_cast<UInt128>(1) << (i * 2);
    }
    return r;
}

UInt128 deinterleaveBits(UInt128 v, size_t bits) {
    UInt128 r = 0;
    for (size_t i = 0; i * 2 < bits; ++i) {
        if ((v >> (i * 2)) & 1) r |= static_cast<UInt128>(1) << i;
    }
    return r;
}

Value bitManipUnary(Opcode::Value op, Type::Value type, const Value& a) {
    requireInt(type, "bit manipulation");
    size_t bits = bitWidth(type);
    UInt128 v = maskToWidth(static_cast<UInt128>(a.asInt128()), bits);
    switch (op) {
        case Opcode::Blsi: return Value::fromInt128(type, static_cast<Int128>(v & (~v + 1)));
        case Opcode::Blsr: return Value::fromInt128(type, static_cast<Int128>(v & (v - 1)));
        case Opcode::Blsmsk: return Value::fromInt128(type, static_cast<Int128>(v ^ (v - 1)));
        case Opcode::Gray: return Value::fromInt128(type, static_cast<Int128>(v ^ (v >> 1)));
        case Opcode::Ungray: {
            UInt128 r = v;
            for (size_t s = 1; s < bits; s <<= 1) r ^= r >> s;
            return Value::fromInt128(type, static_cast<Int128>(r));
        }
        case Opcode::Clrsb: {
            Int128 x = a.asInt128();
            UInt128 u = maskToWidth(static_cast<UInt128>(x), bits);
            int sign = static_cast<int>((u >> (bits - 1)) & 1);
            int n = 0;
            for (size_t i = bits - 1; i-- > 0;) {
                if (static_cast<int>((u >> i) & 1) != sign) break;
                ++n;
            }
            return Value::fromInt128(type, n);
        }
        case Opcode::Zerocount: return Value::fromInt128(type, static_cast<int>(bits) - popcountGeneric(v, bits));
        case Opcode::Hibit: return Value::fromInt128(type, v == 0 ? -1 : static_cast<int>(bits) - 1 - clzGeneric(v, bits));
        case Opcode::Lobit: return Value::fromInt128(type, v == 0 ? -1 : ctzGeneric(v, bits));
        case Opcode::Msbmask:
            return Value::fromInt128(type, ((v >> (bits - 1)) & 1) ? static_cast<Int128>(-1) : 0);
        case Opcode::Nibbleswap: {
            UInt128 r = 0;
            size_t nibbles = bits / 4;
            for (size_t i = 0; i < nibbles; ++i) {
                r |= ((v >> (i * 4)) & 0xF) << ((nibbles - 1 - i) * 4);
            }
            return Value::fromInt128(type, static_cast<Int128>(r));
        }
        case Opcode::Bitrev8: {
            UInt128 r = 0;
            size_t bytes = bits / 8;
            for (size_t b = 0; b < bytes; ++b) {
                UInt128 byte = (v >> (b * 8)) & 0xFF;
                UInt128 rb = bitreverseGeneric(byte, 8);
                r |= rb << (b * 8);
            }
            return Value::fromInt128(type, static_cast<Int128>(r));
        }
        case Opcode::Bswap16:
            return Value::fromInt128(type, static_cast<Int128>((v & ~static_cast<UInt128>(0xFFFF)) |
                                                                bswapGeneric(v & 0xFFFF, 2)));
        case Opcode::Bswap32:
            return Value::fromInt128(type, static_cast<Int128>((v & ~static_cast<UInt128>(0xFFFFFFFFu)) |
                                                                bswapGeneric(v & 0xFFFFFFFFu, 4)));
        case Opcode::Bswap64:
            return Value::fromInt128(type, static_cast<Int128>(bswapGeneric(v, 8)));
        case Opcode::Bitinterleave: return Value::fromInt128(type, static_cast<Int128>(interleaveBits(v, bits)));
        case Opcode::Bitdeinterleave: return Value::fromInt128(type, static_cast<Int128>(deinterleaveBits(v, bits)));
        case Opcode::Lowmask: {
            Int128 n = a.asInt128();
            if (n < 0) throw RuntimeError("lowmask of a negative width");
            return Value::fromInt128(type, static_cast<Int128>(lowMask(static_cast<size_t>(n))));
        }
        case Opcode::Himask: {
            Int128 n = a.asInt128();
            if (n < 0 || static_cast<size_t>(n) > bits) throw RuntimeError("himask width out of range");
            UInt128 low = lowMask(bits - static_cast<size_t>(n));
            return Value::fromInt128(type, static_cast<Int128>(maskToWidth(~low, bits)));
        }
        default: throw RuntimeError("not a unary bit-manipulation opcode");
    }
}

Value bitManipBinary(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    requireInt(type, "bit manipulation");
    size_t bits = bitWidth(type);
    UInt128 x = maskToWidth(static_cast<UInt128>(a.asInt128()), bits);
    UInt128 y = maskToWidth(static_cast<UInt128>(b.asInt128()), bits);
    Int128 n = b.asInt128();
    switch (op) {
        case Opcode::Bext: return Value::fromInt128(type, static_cast<Int128>(bextGeneric(x, y, bits)));
        case Opcode::Bdep: return Value::fromInt128(type, static_cast<Int128>(bdepGeneric(x, y, bits)));
        case Opcode::Sextbit: {
            if (n <= 0 || static_cast<size_t>(n) > bits) throw RuntimeError("sextbit width out of range");
            size_t w = static_cast<size_t>(n);
            UInt128 masked = x & lowMask(w);
            if ((masked >> (w - 1)) & 1) masked |= maskToWidth(~lowMask(w), bits);
            return Value::fromInt128(type, static_cast<Int128>(masked));
        }
        case Opcode::Zextbit: {
            if (n < 0 || static_cast<size_t>(n) > bits) throw RuntimeError("zextbit width out of range");
            return Value::fromInt128(type, static_cast<Int128>(x & lowMask(static_cast<size_t>(n))));
        }
        default: throw RuntimeError("not a binary bit-manipulation opcode");
    }
}

const double kPi = 3.14159265358979323846;

Value makeBool(bool b) { return Value::fromInt128(Type::I32, b ? 1 : 0); }

Value makePtr(uint64_t addr) {
    Value v;
    v.type = Type::Ptr;
    v.bits.ptr = addr;
    return v;
}

void requireFloat(Type::Value type, const char* what) {
    if (!isFloatType(type)) throw RuntimeError(std::string(what) + " requires a float type (f32/f64)");
}

double roundEven(double x) {
    double r = std::floor(x + 0.5);
    if (r - x == 0.5 && std::fmod(r, 2.0) != 0.0) r -= 1.0;
    return r;
}

Value floatUnaryExt(Opcode::Value op, Type::Value type, const Value& a) {
    requireFloat(type, "this operation");
    double x = a.asDouble();
    double r = 0;
    switch (op) {
        case Opcode::Asinh: r = std::log(x + std::sqrt(x * x + 1.0)); break;
        case Opcode::Acosh: r = std::log(x + std::sqrt(x * x - 1.0)); break;
        case Opcode::Atanh: r = 0.5 * std::log((1.0 + x) / (1.0 - x)); break;
        case Opcode::Exp10: r = std::pow(10.0, x); break;
        case Opcode::Expm1: r = std::exp(x) - 1.0; break;
        case Opcode::Log1p: r = std::log(1.0 + x); break;
        case Opcode::Erf: r = std::erf(x); break;
        case Opcode::Erfc: r = std::erfc(x); break;
        case Opcode::Tgamma: r = std::tgamma(x); break;
        case Opcode::Lgamma: r = std::lgamma(x); break;
        case Opcode::Rint:
        case Opcode::Nearbyint:
        case Opcode::Roundeven: r = roundEven(x); break;
        case Opcode::Logb: r = std::floor(std::log(std::fabs(x)) / std::log(2.0)); break;
        case Opcode::Recip: r = 1.0 / x; break;
        case Opcode::Rsqrt: r = 1.0 / std::sqrt(x); break;
        case Opcode::Fract: r = x - std::floor(x); break;
        case Opcode::Fsat: r = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); break;
        case Opcode::Degrees: r = x * (180.0 / kPi); break;
        case Opcode::Radians: r = x * (kPi / 180.0); break;
        case Opcode::Sinpi: r = std::sin(kPi * x); break;
        case Opcode::Cospi: r = std::cos(kPi * x); break;
        case Opcode::Tanpi: r = std::tan(kPi * x); break;
        case Opcode::Frexp: {
            int e = 0;
            r = std::frexp(x, &e);
            break;
        }
        case Opcode::Modf: {
            double ip = 0;
            r = std::modf(x, &ip);
            break;
        }
        case Opcode::Modfi: {
            double ip = 0;
            std::modf(x, &ip);
            r = ip;
            break;
        }
        default: throw RuntimeError("not a unary float opcode");
    }
    return Value::fromDouble(type, r);
}

Value floatBinaryExt(Opcode::Value op, Type::Value type, const Value& a, const Value& b) {
    requireFloat(type, "this operation");
    double x = a.asDouble();
    double y = b.asDouble();
    double r = 0;
    switch (op) {
        case Opcode::Nextafter: r = std::nextafter(x, y); break;
        case Opcode::Ldexp:
        case Opcode::Scalbn: r = std::ldexp(x, static_cast<int>(b.asInt128())); break;
        case Opcode::Fdim: r = (x > y) ? (x - y) : 0.0; break;
        case Opcode::Fmax:
            if (x != x) r = y;
            else if (y != y) r = x;
            else r = (x > y) ? x : y;
            break;
        case Opcode::Fmin:
            if (x != x) r = y;
            else if (y != y) r = x;
            else r = (x < y) ? x : y;
            break;
        default: throw RuntimeError("not a binary float opcode");
    }
    return Value::fromDouble(type, r);
}

Value floatPredicate(Opcode::Value op, Type::Value type, const Value& a) {
    requireFloat(type, "this operation");
    double x = a.asDouble();
    bool isNan = (x != x);
    bool isInf = !isNan && (x > 1.7976931348623157e308 || x < -1.7976931348623157e308);
    double ax = isNan ? 0.0 : (x < 0 ? -x : x);
    bool isZero = !isNan && x == 0.0;
    bool isSub = !isNan && !isInf && !isZero && ax < 2.2250738585072014e-308;
    switch (op) {
        case Opcode::Isnan: return makeBool(isNan);
        case Opcode::Isinf: return makeBool(isInf);
        case Opcode::Isfinite: return makeBool(!isNan && !isInf);
        case Opcode::Isnormal: return makeBool(!isNan && !isInf && !isZero && !isSub);
        case Opcode::Issubnormal: return makeBool(isSub);
        case Opcode::Signbit: {
            if (type == Type::F32) {
                float f = static_cast<float>(x);
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                return makeBool((bits >> 31) != 0);
            }
            uint64_t bits;
            std::memcpy(&bits, &x, 8);
            return makeBool((bits >> 63) != 0);
        }
        case Opcode::Fpclassify: {
            int cls = 4;
            if (isNan) cls = 0;
            else if (isInf) cls = 1;
            else if (isZero) cls = 2;
            else if (isSub) cls = 3;
            return Value::fromInt128(Type::I32, cls);
        }
        case Opcode::Ilogb: {
            int e = 0;
            std::frexp(x, &e);
            return Value::fromInt128(Type::I32, e - 1);
        }
        case Opcode::Frexpe: {
            int e = 0;
            std::frexp(x, &e);
            return Value::fromInt128(Type::I32, e);
        }
        default: throw RuntimeError("not a float predicate opcode");
    }
}

bool compareResult(Opcode::Value op, int flag) {
    switch (op) {
        case Opcode::Cmpeq:
        case Opcode::Seteq:
        case Opcode::Breq: return flag == 0;
        case Opcode::Cmpne:
        case Opcode::Setne:
        case Opcode::Brne: return flag != 0;
        case Opcode::Cmplt:
        case Opcode::Setlt:
        case Opcode::Brlt:
        case Opcode::Ucmplt:
        case Opcode::Ubrlt: return flag < 0;
        case Opcode::Cmpgt:
        case Opcode::Setgt:
        case Opcode::Brgt:
        case Opcode::Ucmpgt:
        case Opcode::Ubrgt: return flag > 0;
        case Opcode::Cmple:
        case Opcode::Setle:
        case Opcode::Brle:
        case Opcode::Ucmple:
        case Opcode::Ubrle: return flag <= 0;
        case Opcode::Cmpge:
        case Opcode::Setge:
        case Opcode::Brge:
        case Opcode::Ucmpge:
        case Opcode::Ubrge: return flag >= 0;
        default: throw RuntimeError("not a comparison opcode");
    }
}

int unsignedCompare(Type::Value type, const Value& a, const Value& b) {
    UInt128 x = unsignedOf(a, type);
    UInt128 y = unsignedOf(b, type);
    return x < y ? -1 : (x > y ? 1 : 0);
}

Value convertOp(Opcode::Value op, Type::Value from, Type::Value to, const Value& a) {
    size_t fromBits = bitWidth(from);
    switch (op) {
        case Opcode::Sext: {
            UInt128 raw = maskToWidth(static_cast<UInt128>(a.asInt128()), fromBits);
            Int128 v = static_cast<Int128>(raw);
            if (fromBits < 128 && (raw >> (fromBits - 1)) & 1) {
                v = static_cast<Int128>(raw | ~unsignedMaxOfWidth(fromBits));
            }
            return Value::fromInt128(to, v);
        }
        case Opcode::Zext:
        case Opcode::Itrunc:
            return Value::fromInt128(to, static_cast<Int128>(maskToWidth(static_cast<UInt128>(a.asInt128()), fromBits)));
        case Opcode::Fpext:
        case Opcode::Fptrunc:
            if (!isFloatType(from) || !isFloatType(to)) throw RuntimeError("fpext/fptrunc require float types");
            return Value::fromDouble(to, a.asDouble());
        case Opcode::Fptosi:
            if (!isFloatType(from)) throw RuntimeError("fptosi source must be a float type");
            return Value::fromInt128(to, static_cast<Int128>(a.asDouble()));
        case Opcode::Fptoui: {
            if (!isFloatType(from)) throw RuntimeError("fptoui source must be a float type");
            double d = a.asDouble();
            if (d < 0.0) d = 0.0;
            return Value::fromInt128(to, static_cast<Int128>(static_cast<UInt128>(d)));
        }
        case Opcode::Sitofp:
            if (!isFloatType(to)) throw RuntimeError("sitofp destination must be a float type");
            return Value::fromDouble(to, static_cast<double>(a.asInt128()));
        case Opcode::Uitofp:
            if (!isFloatType(to)) throw RuntimeError("uitofp destination must be a float type");
            return Value::fromDouble(to, static_cast<double>(unsignedOf(a, from)));
        case Opcode::Bitconv: {
            Value src = a;
            src.type = from;
            return bitcast(to, src);
        }
        default: throw RuntimeError("not a conversion opcode");
    }
}

const size_t kMemorySize = 1u << 20;

const size_t kStackSize = 1u << 16;

struct HeapBlock {
    uint64_t offset;
    uint64_t size;
    bool free;
};

class Interpreter {
public:
    explicit Interpreter(const Program& program)
        : program_(program), memory_(kMemorySize, 0), stack_(kStackSize, 0), stackPointer_(kStackSize),
          heapBumpPtr_(0) {
        for (size_t i = 0; i < program_.functions.size(); ++i) {
            functionIndex_[program_.functions[i].name] = i;
        }
    }

    Value call(size_t functionIdx, const std::vector<Value>& args) {
        const Function& fn = program_.functions[functionIdx];
        if (args.size() != fn.params.size()) {
            throw RuntimeError("call to '" + fn.name + "' expects " + toString(fn.params.size()) +
                                " argument(s), got " + toString(args.size()));
        }

        Frame frame;
        for (size_t i = 0; i < args.size(); ++i) frame.reg(static_cast<uint32_t>(i)) = args[i];

        NameIndexMap blockIndex;
        for (size_t i = 0; i < fn.blocks.size(); ++i) blockIndex[fn.blocks[i].label] = i;

        if (fn.blocks.empty()) throw RuntimeError("function '" + fn.name + "' has no blocks");
        size_t blockIdx = 0;

        while (true) {
            const Block& block = fn.blocks[blockIdx];
            bool jumped = false;

            for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
                const Instruction& instr = block.instructions[ii];
                ++frame.pc;
                switch (instr.opcode) {
                    case Opcode::OpcodeCount:
                        throw RuntimeError("invalid opcode in instruction stream");
                    case Opcode::Mov: {
                        frame.reg(instr.dest) = readOperand(frame, instr.operands[0], instr.type);
                        break;
                    }
                    case Opcode::Add:
                    case Opcode::Sub:
                    case Opcode::Mul:
                    case Opcode::Div:
                    case Opcode::Mod: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = arith(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::And:
                    case Opcode::Or:
                    case Opcode::Xor: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = bitwise(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Not: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = bitwiseNot(instr.type, a);
                        break;
                    }
                    case Opcode::Shl:
                    case Opcode::Shr: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value amount = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = shift(instr.opcode, instr.type, a, amount);
                        break;
                    }
                    case Opcode::Neg: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = negate(instr.type, a);
                        break;
                    }
                    case Opcode::Push: {
                        Value v = readOperand(frame, instr.operands[0], instr.type);
                        pushValue(v);
                        break;
                    }
                    case Opcode::Pop: {
                        frame.reg(instr.dest) = popValue(instr.type);
                        break;
                    }
                    case Opcode::Convert: {
                        Value src = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = isFloatType(instr.type)
                                                     ? Value::fromDouble(instr.type, src.asDouble())
                                                     : Value::fromInt128(instr.type, src.asInt128());
                        break;
                    }
                    case Opcode::Cast: {
                        Value src = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = bitcast(instr.type, src);
                        break;
                    }
                    case Opcode::Cmp: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.cmpFlag = compare(instr.type, a, b);
                        break;
                    }
                    case Opcode::Jmp: {
                        blockIdx = resolveBlock(blockIndex, instr.operands[0].name, fn.name);
                        jumped = true;
                        break;
                    }
                    case Opcode::Beq:
                    case Opcode::Bne:
                    case Opcode::Blt:
                    case Opcode::Bgt:
                    case Opcode::Ble:
                    case Opcode::Bge: {
                        if (shouldBranch(instr.opcode, frame.cmpFlag)) {
                            blockIdx = resolveBlock(blockIndex, instr.operands[0].name, fn.name);
                            jumped = true;
                        }
                        break;
                    }
                    case Opcode::Call: {
                        NameIndexMap::const_iterator it = functionIndex_.find(instr.operands[0].name);
                        if (it == functionIndex_.end()) {
                            throw RuntimeError("call to unknown function '" + instr.operands[0].name + "'");
                        }
                        std::vector<Value> callArgs;
                        for (size_t i = 1; i < instr.operands.size(); ++i) {
                            callArgs.push_back(readOperand(frame, instr.operands[i], Type::I64));
                        }
                        Value result = call(it->second, callArgs);
                        if (instr.hasDest) frame.reg(instr.dest) = result;
                        break;
                    }
                    case Opcode::Ret: {
                        Value rv = readOperand(frame, instr.operands[0], instr.type);
                        freeAllocas(frame);
                        return rv;
                    }
                    case Opcode::RetVoid: {
                        freeAllocas(frame);
                        Value v;
                        v.type = Type::Void;
                        return v;
                    }
                    case Opcode::Load: {
                        Value addr = readOperand(frame, instr.operands[0], Type::Ptr);
                        frame.reg(instr.dest) = loadValue(addr.bits.ptr, instr.type);
                        break;
                    }
                    case Opcode::Store: {
                        Value addr = readOperand(frame, instr.operands[0], Type::Ptr);
                        Value value = readOperand(frame, instr.operands[1], instr.type);
                        storeValue(addr.bits.ptr, value);
                        break;
                    }
                    case Opcode::Alloc: {
                        Value size = readOperand(frame, instr.operands[0], instr.type);
                        uint64_t off = heapAlloc(static_cast<uint64_t>(size.asInt128()));
                        Value v;
                        v.type = Type::Ptr;
                        v.bits.ptr = off;
                        frame.reg(instr.dest) = v;
                        break;
                    }
                    case Opcode::Free: {
                        Value ptr = readOperand(frame, instr.operands[0], Type::Ptr);
                        heapFree(ptr.bits.ptr);
                        break;
                    }
                    case Opcode::Realloc: {
                        Value ptr = readOperand(frame, instr.operands[0], Type::Ptr);
                        Value size = readOperand(frame, instr.operands[1], instr.type);
                        uint64_t newOff = heapRealloc(ptr.bits.ptr, static_cast<uint64_t>(size.asInt128()));
                        Value v;
                        v.type = Type::Ptr;
                        v.bits.ptr = newOff;
                        frame.reg(instr.dest) = v;
                        break;
                    }
                    case Opcode::MemCpy:
                    case Opcode::MemMove: {
                        Value dst = readOperand(frame, instr.operands[0], Type::Ptr);
                        Value src = readOperand(frame, instr.operands[1], Type::Ptr);
                        Value len = readOperand(frame, instr.operands[2], instr.type);
                        memRangeOp(instr.opcode, dst.bits.ptr, src.bits.ptr, static_cast<uint64_t>(len.asInt128()));
                        break;
                    }
                    case Opcode::MemSet: {
                        Value dst = readOperand(frame, instr.operands[0], Type::Ptr);
                        Value value = readOperand(frame, instr.operands[1], instr.type);
                        Value len = readOperand(frame, instr.operands[2], instr.type);
                        memSetOp(dst.bits.ptr, static_cast<uint8_t>(value.asInt128() & 0xFF),
                                 static_cast<uint64_t>(len.asInt128()));
                        break;
                    }
                    case Opcode::MemCmp: {
                        Value a = readOperand(frame, instr.operands[0], Type::Ptr);
                        Value b = readOperand(frame, instr.operands[1], Type::Ptr);
                        Value len = readOperand(frame, instr.operands[2], instr.type);
                        int r = memCmpOp(a.bits.ptr, b.bits.ptr, static_cast<uint64_t>(len.asInt128()));
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Sqrt:
                    case Opcode::Cbrt:
                    case Opcode::Floor:
                    case Opcode::Ceil:
                    case Opcode::Round:
                    case Opcode::Trunc:
                    case Opcode::Sin:
                    case Opcode::Cos:
                    case Opcode::Tan:
                    case Opcode::Asin:
                    case Opcode::Acos:
                    case Opcode::Atan:
                    case Opcode::Sinh:
                    case Opcode::Cosh:
                    case Opcode::Tanh:
                    case Opcode::Log:
                    case Opcode::Log2:
                    case Opcode::Log10:
                    case Opcode::Exp:
                    case Opcode::Exp2: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = floatUnary(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Abs: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = absValue(instr.type, a);
                        break;
                    }
                    case Opcode::Min:
                    case Opcode::Max: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = minMaxValue(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Pow:
                    case Opcode::Atan2:
                    case Opcode::Hypot:
                    case Opcode::Copysign:
                    case Opcode::Fmod: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = floatBinary(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Fma: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Value c = readOperand(frame, instr.operands[2], instr.type);
                        frame.reg(instr.dest) = fmaValue(instr.type, a, b, c);
                        break;
                    }
                    case Opcode::Popcount:
                    case Opcode::Clz:
                    case Opcode::Ctz:
                    case Opcode::Parity:
                    case Opcode::Ffs:
                    case Opcode::Bitreverse:
                    case Opcode::Bswap: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = bitUnary(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Rotl:
                    case Opcode::Rotr: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value amount = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = rotateValue(instr.opcode, instr.type, a, amount);
                        break;
                    }
                    case Opcode::Bitset:
                    case Opcode::Bitclear:
                    case Opcode::Bittest: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value idx = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = bitIndexOp(instr.opcode, instr.type, a, idx);
                        break;
                    }
                    case Opcode::Udiv:
                    case Opcode::Umod:
                    case Opcode::Sar:
                    case Opcode::Mulhi:
                    case Opcode::Umulhi:
                    case Opcode::Umin:
                    case Opcode::Umax:
                    case Opcode::Absdiff:
                    case Opcode::Uabsdiff:
                    case Opcode::Avg:
                    case Opcode::Uavg:
                    case Opcode::Nand:
                    case Opcode::Nor:
                    case Opcode::Xnor:
                    case Opcode::Andnot:
                    case Opcode::Ornot:
                    case Opcode::Powi:
                    case Opcode::Gcd:
                    case Opcode::Lcm:
                    case Opcode::Divceil:
                    case Opcode::Divfloor:
                    case Opcode::Diveuclid:
                    case Opcode::Modeuclid:
                    case Opcode::Alignup:
                    case Opcode::Aligndown: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = intBinaryExt(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Inc:
                    case Opcode::Dec:
                    case Opcode::Signum:
                    case Opcode::Sqr:
                    case Opcode::Isqrt:
                    case Opcode::Ilog2:
                    case Opcode::Ilog10:
                    case Opcode::Ispow2:
                    case Opcode::Nextpow2:
                    case Opcode::Prevpow2:
                    case Opcode::Negsat: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = intUnaryExt(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Addc:
                    case Opcode::Subb: {
                        requireInt(instr.type, "addc/subb");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        UInt128 ux = unsignedOf(a, instr.type);
                        UInt128 uy = unsignedOf(b, instr.type);
                        UInt128 c = frame.carryFlag ? 1 : 0;
                        UInt128 umax = unsignedMaxOfWidth(bitWidth(instr.type));
                        UInt128 r;
                        if (instr.opcode == Opcode::Addc) {
                            r = ux + uy + c;
                            frame.carryFlag = (r > umax) || (r < ux);
                        } else {
                            r = ux - uy - c;
                            frame.carryFlag = ux < (uy + c);
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(r));
                        break;
                    }
                    case Opcode::Mulwide:
                    case Opcode::Umulwide: {
                        requireInt(instr.type, "mulwide");
                        if (bitWidth(instr.type) > 64) throw RuntimeError("mulwide is not defined for 128-bit types");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Int128 r;
                        if (instr.opcode == Opcode::Mulwide) {
                            r = a.asInt128() * b.asInt128();
                        } else {
                            r = static_cast<Int128>(unsignedOf(a, instr.type) * unsignedOf(b, instr.type));
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type2, r);
                        break;
                    }
                    case Opcode::Ucmp: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        UInt128 ux = unsignedOf(a, instr.type);
                        UInt128 uy = unsignedOf(b, instr.type);
                        frame.cmpFlag = ux < uy ? -1 : (ux > uy ? 1 : 0);
                        break;
                    }
                    case Opcode::Clamp:
                    case Opcode::Uclamp: {
                        requireInt(instr.type, "clamp");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value lo = readOperand(frame, instr.operands[1], instr.type);
                        Value hi = readOperand(frame, instr.operands[2], instr.type);
                        if (instr.opcode == Opcode::Clamp) {
                            Int128 x = a.asInt128();
                            Int128 l = lo.asInt128();
                            Int128 h = hi.asInt128();
                            frame.reg(instr.dest) = Value::fromInt128(instr.type, x < l ? l : (x > h ? h : x));
                        } else {
                            UInt128 x = unsignedOf(a, instr.type);
                            UInt128 l = unsignedOf(lo, instr.type);
                            UInt128 h = unsignedOf(hi, instr.type);
                            frame.reg(instr.dest) =
                                Value::fromInt128(instr.type, static_cast<Int128>(x < l ? l : (x > h ? h : x)));
                        }
                        break;
                    }
                    case Opcode::Mla:
                    case Opcode::Mls: {
                        requireInt(instr.type, "mla/mls");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Value c = readOperand(frame, instr.operands[2], instr.type);
                        Int128 prod = a.asInt128() * b.asInt128();
                        Int128 r = (instr.opcode == Opcode::Mla) ? (prod + c.asInt128()) : (c.asInt128() - prod);
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Addsat:
                    case Opcode::Subsat:
                    case Opcode::Mulsat:
                    case Opcode::Shlsat:
                    case Opcode::Uaddsat:
                    case Opcode::Usubsat:
                    case Opcode::Umulsat: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = saturatingOp(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Addo:
                    case Opcode::Subo:
                    case Opcode::Mulo:
                    case Opcode::Shlo:
                    case Opcode::Uaddo:
                    case Opcode::Usubo:
                    case Opcode::Umulo: {
                        requireInt(instr.type, "checked arithmetic");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Int128 r = 0;
                        frame.overflowFlag = computeWithOverflow(instr.opcode, instr.type, a, b, r);
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Nego: {
                        requireInt(instr.type, "checked arithmetic");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value zero = Value::fromInt128(instr.type, 0);
                        Int128 r = 0;
                        frame.overflowFlag = computeWithOverflow(instr.opcode, instr.type, a, zero, r);
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Addchk:
                    case Opcode::Subchk:
                    case Opcode::Mulchk:
                    case Opcode::Uaddchk:
                    case Opcode::Usubchk:
                    case Opcode::Umulchk: {
                        requireInt(instr.type, "checked arithmetic");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Int128 r = 0;
                        if (computeWithOverflow(instr.opcode, instr.type, a, b, r)) {
                            throw RuntimeError(std::string(opcodeName(instr.opcode)) + " overflowed");
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Divchk: {
                        requireInt(instr.type, "divchk");
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        Int128 x = a.asInt128();
                        Int128 y = b.asInt128();
                        if (y == 0) throw RuntimeError("division by zero");
                        if (x == signedMinOfWidth(bitWidth(instr.type)) && y == -1) {
                            throw RuntimeError("divchk overflowed");
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, x / y);
                        break;
                    }
                    case Opcode::Seto: {
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, frame.overflowFlag ? 1 : 0);
                        break;
                    }
                    case Opcode::Clro: {
                        frame.overflowFlag = false;
                        frame.carryFlag = false;
                        break;
                    }
                    case Opcode::Sext:
                    case Opcode::Zext:
                    case Opcode::Itrunc:
                    case Opcode::Fpext:
                    case Opcode::Fptrunc:
                    case Opcode::Fptosi:
                    case Opcode::Fptoui:
                    case Opcode::Sitofp:
                    case Opcode::Uitofp:
                    case Opcode::Bitconv: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = convertOp(instr.opcode, instr.type, instr.type2, a);
                        break;
                    }
                    case Opcode::Asinh:
                    case Opcode::Acosh:
                    case Opcode::Atanh:
                    case Opcode::Exp10:
                    case Opcode::Expm1:
                    case Opcode::Log1p:
                    case Opcode::Erf:
                    case Opcode::Erfc:
                    case Opcode::Tgamma:
                    case Opcode::Lgamma:
                    case Opcode::Rint:
                    case Opcode::Nearbyint:
                    case Opcode::Roundeven:
                    case Opcode::Logb:
                    case Opcode::Recip:
                    case Opcode::Rsqrt:
                    case Opcode::Fract:
                    case Opcode::Fsat:
                    case Opcode::Degrees:
                    case Opcode::Radians:
                    case Opcode::Sinpi:
                    case Opcode::Cospi:
                    case Opcode::Tanpi:
                    case Opcode::Frexp:
                    case Opcode::Modf:
                    case Opcode::Modfi: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = floatUnaryExt(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Nextafter:
                    case Opcode::Ldexp:
                    case Opcode::Scalbn:
                    case Opcode::Fdim:
                    case Opcode::Fmax:
                    case Opcode::Fmin: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1],
                                              (instr.opcode == Opcode::Ldexp || instr.opcode == Opcode::Scalbn)
                                                  ? Type::I32
                                                  : instr.type);
                        frame.reg(instr.dest) = floatBinaryExt(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Isnan:
                    case Opcode::Isinf:
                    case Opcode::Isfinite:
                    case Opcode::Isnormal:
                    case Opcode::Issubnormal:
                    case Opcode::Signbit:
                    case Opcode::Fpclassify:
                    case Opcode::Ilogb:
                    case Opcode::Frexpe: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = floatPredicate(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Lerp: {
                        requireFloat(instr.type, "lerp");
                        double a = readOperand(frame, instr.operands[0], instr.type).asDouble();
                        double b = readOperand(frame, instr.operands[1], instr.type).asDouble();
                        double t = readOperand(frame, instr.operands[2], instr.type).asDouble();
                        frame.reg(instr.dest) = Value::fromDouble(instr.type, a + t * (b - a));
                        break;
                    }
                    case Opcode::Fclamp: {
                        requireFloat(instr.type, "fclamp");
                        double x = readOperand(frame, instr.operands[0], instr.type).asDouble();
                        double lo = readOperand(frame, instr.operands[1], instr.type).asDouble();
                        double hi = readOperand(frame, instr.operands[2], instr.type).asDouble();
                        frame.reg(instr.dest) = Value::fromDouble(instr.type, x < lo ? lo : (x > hi ? hi : x));
                        break;
                    }
                    case Opcode::Nan: {
                        requireFloat(instr.type, "nan");
                        frame.reg(instr.dest) = Value::fromDouble(instr.type, std::sqrt(-1.0));
                        break;
                    }
                    case Opcode::Inf: {
                        requireFloat(instr.type, "inf");
                        frame.reg(instr.dest) = Value::fromDouble(instr.type, 1.0 / 0.0);
                        break;
                    }
                    case Opcode::Switch: {
                        Value v = readOperand(frame, instr.operands[0], instr.type);
                        Int128 idx = v.asInt128();
                        size_t caseCount = instr.operands.size() >= 2 ? instr.operands.size() - 2 : 0;
                        const Operand* target = &instr.operands[1];
                        if (idx >= 0 && static_cast<size_t>(idx) < caseCount) {
                            target = &instr.operands[2 + static_cast<size_t>(idx)];
                        }
                        blockIdx = resolveBlock(blockIndex, target->name, fn.name);
                        jumped = true;
                        break;
                    }
                    case Opcode::Select: {
                        Value c = readOperand(frame, instr.operands[0], Type::I32);
                        Value a = readOperand(frame, instr.operands[1], instr.type);
                        Value b = readOperand(frame, instr.operands[2], instr.type);
                        frame.reg(instr.dest) = (c.asInt128() != 0) ? a : b;
                        break;
                    }
                    case Opcode::Seteq:
                    case Opcode::Setne:
                    case Opcode::Setlt:
                    case Opcode::Setgt:
                    case Opcode::Setle:
                    case Opcode::Setge: {
                        frame.reg(instr.dest) = makeBool(compareResult(instr.opcode, frame.cmpFlag));
                        break;
                    }
                    case Opcode::Setc: {
                        frame.reg(instr.dest) = makeBool(frame.carryFlag);
                        break;
                    }
                    case Opcode::Setnc: {
                        frame.reg(instr.dest) = makeBool(!frame.carryFlag);
                        break;
                    }
                    case Opcode::Seteqz:
                    case Opcode::Setnez: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        bool isZero = a.asInt128() == 0;
                        frame.reg(instr.dest) = makeBool(instr.opcode == Opcode::Seteqz ? isZero : !isZero);
                        break;
                    }
                    case Opcode::Cmpeq:
                    case Opcode::Cmpne:
                    case Opcode::Cmplt:
                    case Opcode::Cmpgt:
                    case Opcode::Cmple:
                    case Opcode::Cmpge: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = makeBool(compareResult(instr.opcode, compare(instr.type, a, b)));
                        break;
                    }
                    case Opcode::Ucmplt:
                    case Opcode::Ucmpgt:
                    case Opcode::Ucmple:
                    case Opcode::Ucmpge: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = makeBool(compareResult(instr.opcode, unsignedCompare(instr.type, a, b)));
                        break;
                    }
                    case Opcode::Cmpord:
                    case Opcode::Cmpuno: {
                        double x = readOperand(frame, instr.operands[0], instr.type).asDouble();
                        double y = readOperand(frame, instr.operands[1], instr.type).asDouble();
                        bool unordered = (x != x) || (y != y);
                        frame.reg(instr.dest) = makeBool(instr.opcode == Opcode::Cmpuno ? unordered : !unordered);
                        break;
                    }
                    case Opcode::Brz:
                    case Opcode::Brnz: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        bool isZero = a.asInt128() == 0;
                        if (instr.opcode == Opcode::Brz ? isZero : !isZero) {
                            blockIdx = resolveBlock(blockIndex, instr.operands[1].name, fn.name);
                            jumped = true;
                        }
                        break;
                    }
                    case Opcode::Breq:
                    case Opcode::Brne:
                    case Opcode::Brlt:
                    case Opcode::Brgt:
                    case Opcode::Brle:
                    case Opcode::Brge: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        if (compareResult(instr.opcode, compare(instr.type, a, b))) {
                            blockIdx = resolveBlock(blockIndex, instr.operands[2].name, fn.name);
                            jumped = true;
                        }
                        break;
                    }
                    case Opcode::Ubrlt:
                    case Opcode::Ubrgt:
                    case Opcode::Ubrle:
                    case Opcode::Ubrge: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        if (compareResult(instr.opcode, unsignedCompare(instr.type, a, b))) {
                            blockIdx = resolveBlock(blockIndex, instr.operands[2].name, fn.name);
                            jumped = true;
                        }
                        break;
                    }
                    case Opcode::Bcarry:
                    case Opcode::Bncarry:
                    case Opcode::Bovf:
                    case Opcode::Bnovf: {
                        bool take = false;
                        if (instr.opcode == Opcode::Bcarry) take = frame.carryFlag;
                        else if (instr.opcode == Opcode::Bncarry) take = !frame.carryFlag;
                        else if (instr.opcode == Opcode::Bovf) take = frame.overflowFlag;
                        else take = !frame.overflowFlag;
                        if (take) {
                            blockIdx = resolveBlock(blockIndex, instr.operands[0].name, fn.name);
                            jumped = true;
                        }
                        break;
                    }
                    case Opcode::Tailcall: {
                        NameIndexMap::const_iterator it = functionIndex_.find(instr.operands[0].name);
                        if (it == functionIndex_.end()) {
                            throw RuntimeError("tailcall to unknown function '" + instr.operands[0].name + "'");
                        }
                        std::vector<Value> callArgs;
                        for (size_t i = 1; i < instr.operands.size(); ++i) {
                            callArgs.push_back(readOperand(frame, instr.operands[i], Type::I64));
                        }
                        freeAllocas(frame);
                        return call(it->second, callArgs);
                    }
                    case Opcode::Callindirect: {
                        Value idx = readOperand(frame, instr.operands[0], Type::I64);
                        Int128 fi = idx.asInt128();
                        if (fi < 0 || static_cast<size_t>(fi) >= program_.functions.size()) {
                            throw RuntimeError("callindirect to out-of-range function index " + toString(static_cast<int64_t>(fi)));
                        }
                        std::vector<Value> callArgs;
                        for (size_t i = 1; i < instr.operands.size(); ++i) {
                            callArgs.push_back(readOperand(frame, instr.operands[i], Type::I64));
                        }
                        Value result = call(static_cast<size_t>(fi), callArgs);
                        if (instr.hasDest) frame.reg(instr.dest) = result;
                        break;
                    }
                    case Opcode::Trap:
                        throw RuntimeError("trap");
                    case Opcode::Unreachable:
                        throw RuntimeError("reached an unreachable instruction");
                    case Opcode::Abort:
                        throw RuntimeError("abort");
                    case Opcode::Nop:
                        break;
                    case Opcode::Enter: {
                        Value n = readOperand(frame, instr.operands[0], instr.type);
                        frame.enterSize = static_cast<uint64_t>(n.asInt128());
                        break;
                    }
                    case Opcode::Leave: {
                        frame.enterSize = 0;
                        break;
                    }
                    case Opcode::Alloca: {
                        Value n = readOperand(frame, instr.operands[0], instr.type);
                        uint64_t off = heapAlloc(static_cast<uint64_t>(n.asInt128()));
                        frame.allocas.push_back(off);
                        frame.reg(instr.dest) = makePtr(off);
                        break;
                    }
                    case Opcode::Getsp:
                    case Opcode::Stacksave: {
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(stackPointer_));
                        break;
                    }
                    case Opcode::Setsp:
                    case Opcode::Stackrestore: {
                        Value v = readOperand(frame, instr.operands[0], instr.type);
                        Int128 sp = v.asInt128();
                        if (sp < 0 || static_cast<size_t>(sp) > stack_.size()) throw RuntimeError("stack pointer out of range");
                        stackPointer_ = static_cast<size_t>(sp);
                        break;
                    }
                    case Opcode::Getfp: {
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(frame.framePointer));
                        break;
                    }
                    case Opcode::Setfp: {
                        Value v = readOperand(frame, instr.operands[0], instr.type);
                        frame.framePointer = static_cast<uint64_t>(v.asInt128());
                        break;
                    }
                    case Opcode::Getpc: {
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(frame.pc));
                        break;
                    }
                    case Opcode::Framesize: {
                        frame.reg(instr.dest) =
                            Value::fromInt128(instr.type, static_cast<Int128>(frame.regs.size() * 8 + frame.enterSize));
                        break;
                    }
                    case Opcode::Stackdepth: {
                        frame.reg(instr.dest) =
                            Value::fromInt128(instr.type, static_cast<Int128>(stack_.size() - stackPointer_));
                        break;
                    }
                    case Opcode::Stackfree: {
                        freeAllocas(frame);
                        break;
                    }
                    case Opcode::Pushf: {
                        int32_t packed = frame.cmpFlag & 0xFF;
                        if (frame.carryFlag) packed |= 0x100;
                        if (frame.overflowFlag) packed |= 0x200;
                        pushValue(Value::fromInt128(Type::I32, packed));
                        break;
                    }
                    case Opcode::Popf: {
                        Value v = popValue(Type::I32);
                        int32_t packed = static_cast<int32_t>(v.asInt128());
                        frame.cmpFlag = static_cast<signed char>(packed & 0xFF);
                        frame.carryFlag = (packed & 0x100) != 0;
                        frame.overflowFlag = (packed & 0x200) != 0;
                        break;
                    }
                    case Opcode::Getelementptr: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 index = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        Int128 scale = readOperand(frame, instr.operands[2], Type::I64).asInt128();
                        Int128 offset = instr.operands.size() > 3
                                            ? readOperand(frame, instr.operands[3], Type::I64).asInt128()
                                            : 0;
                        frame.reg(instr.dest) =
                            makePtr(static_cast<uint64_t>(static_cast<Int128>(base) + index * scale + offset));
                        break;
                    }
                    case Opcode::Ptradd:
                    case Opcode::Ptrsub: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 off = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        Int128 r = (instr.opcode == Opcode::Ptradd) ? (static_cast<Int128>(base) + off)
                                                                     : (static_cast<Int128>(base) - off);
                        frame.reg(instr.dest) = makePtr(static_cast<uint64_t>(r));
                        break;
                    }
                    case Opcode::Ptrdiff: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t b = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        frame.reg(instr.dest) =
                            Value::fromInt128(instr.type, static_cast<Int128>(a) - static_cast<Int128>(b));
                        break;
                    }
                    case Opcode::Fieldload:
                    case Opcode::Loadv:
                    case Opcode::Loadu: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 off = instr.operands.size() > 1
                                         ? readOperand(frame, instr.operands[1], Type::I64).asInt128()
                                         : 0;
                        frame.reg(instr.dest) = loadValue(static_cast<uint64_t>(static_cast<Int128>(base) + off), instr.type);
                        break;
                    }
                    case Opcode::Fieldstore:
                    case Opcode::Storev:
                    case Opcode::Storeu: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        bool hasOffset = instr.operands.size() > 2;
                        Int128 off = hasOffset ? readOperand(frame, instr.operands[1], Type::I64).asInt128() : 0;
                        const Operand& valueOp = hasOffset ? instr.operands[2] : instr.operands[1];
                        Value value = readOperand(frame, valueOp, instr.type);
                        storeValue(static_cast<uint64_t>(static_cast<Int128>(base) + off), value);
                        break;
                    }
                    case Opcode::Arrayload: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 index = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        Int128 addr = static_cast<Int128>(base) + index * static_cast<Int128>(sizeOfType(instr.type));
                        frame.reg(instr.dest) = loadValue(static_cast<uint64_t>(addr), instr.type);
                        break;
                    }
                    case Opcode::Arraystore: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 index = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        Value value = readOperand(frame, instr.operands[2], instr.type);
                        Int128 addr = static_cast<Int128>(base) + index * static_cast<Int128>(sizeOfType(instr.type));
                        storeValue(static_cast<uint64_t>(addr), value);
                        break;
                    }
                    case Opcode::Ptrtoint: {
                        uint64_t p = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(p));
                        break;
                    }
                    case Opcode::Inttoptr: {
                        Value v = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = makePtr(static_cast<uint64_t>(v.asInt128()));
                        break;
                    }
                    case Opcode::Ptreq:
                    case Opcode::Ptrne:
                    case Opcode::Ptrlt:
                    case Opcode::Ptrgt: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t b = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        bool r = false;
                        if (instr.opcode == Opcode::Ptreq) r = a == b;
                        else if (instr.opcode == Opcode::Ptrne) r = a != b;
                        else if (instr.opcode == Opcode::Ptrlt) r = a < b;
                        else r = a > b;
                        frame.reg(instr.dest) = makeBool(r);
                        break;
                    }
                    case Opcode::Ptrisnull: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        frame.reg(instr.dest) = makeBool(a == 0);
                        break;
                    }
                    case Opcode::Ptralign: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Int128 n = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        if (n <= 0) throw RuntimeError("ptralign to a non-positive alignment");
                        uint64_t align = static_cast<uint64_t>(n);
                        frame.reg(instr.dest) = makePtr(((a + align - 1) / align) * align);
                        break;
                    }
                    case Opcode::Blsi:
                    case Opcode::Blsr:
                    case Opcode::Blsmsk:
                    case Opcode::Gray:
                    case Opcode::Ungray:
                    case Opcode::Clrsb:
                    case Opcode::Zerocount:
                    case Opcode::Hibit:
                    case Opcode::Lobit:
                    case Opcode::Msbmask:
                    case Opcode::Nibbleswap:
                    case Opcode::Bitrev8:
                    case Opcode::Bswap16:
                    case Opcode::Bswap32:
                    case Opcode::Bswap64:
                    case Opcode::Bitinterleave:
                    case Opcode::Bitdeinterleave:
                    case Opcode::Lowmask:
                    case Opcode::Himask: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        frame.reg(instr.dest) = bitManipUnary(instr.opcode, instr.type, a);
                        break;
                    }
                    case Opcode::Bext:
                    case Opcode::Bdep:
                    case Opcode::Sextbit:
                    case Opcode::Zextbit: {
                        Value a = readOperand(frame, instr.operands[0], instr.type);
                        Value b = readOperand(frame, instr.operands[1], instr.type);
                        frame.reg(instr.dest) = bitManipBinary(instr.opcode, instr.type, a, b);
                        break;
                    }
                    case Opcode::Bfextract: {
                        requireInt(instr.type, "bfextract");
                        size_t bits = bitWidth(instr.type);
                        UInt128 v = unsignedOf(readOperand(frame, instr.operands[0], instr.type), instr.type);
                        Int128 start = readOperand(frame, instr.operands[1], Type::I64).asInt128();
                        Int128 len = readOperand(frame, instr.operands[2], Type::I64).asInt128();
                        if (start < 0 || len < 0 || static_cast<size_t>(start + len) > bits) {
                            throw RuntimeError("bitfield range out of bounds");
                        }
                        UInt128 r = (v >> static_cast<size_t>(start)) & lowMask(static_cast<size_t>(len));
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(r));
                        break;
                    }
                    case Opcode::Bfinsert:
                    case Opcode::Bfclear:
                    case Opcode::Bfset: {
                        requireInt(instr.type, "bitfield op");
                        size_t bits = bitWidth(instr.type);
                        UInt128 v = unsignedOf(readOperand(frame, instr.operands[0], instr.type), instr.type);
                        size_t argBase = (instr.opcode == Opcode::Bfinsert) ? 2 : 1;
                        UInt128 field = (instr.opcode == Opcode::Bfinsert)
                                            ? unsignedOf(readOperand(frame, instr.operands[1], instr.type), instr.type)
                                            : 0;
                        Int128 start = readOperand(frame, instr.operands[argBase], Type::I64).asInt128();
                        Int128 len = readOperand(frame, instr.operands[argBase + 1], Type::I64).asInt128();
                        if (start < 0 || len < 0 || static_cast<size_t>(start + len) > bits) {
                            throw RuntimeError("bitfield range out of bounds");
                        }
                        UInt128 mask = lowMask(static_cast<size_t>(len)) << static_cast<size_t>(start);
                        UInt128 r;
                        if (instr.opcode == Opcode::Bfinsert) {
                            r = (v & ~mask) | ((field << static_cast<size_t>(start)) & mask);
                        } else if (instr.opcode == Opcode::Bfclear) {
                            r = v & ~mask;
                        } else {
                            r = v | mask;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(maskToWidth(r, bits)));
                        break;
                    }
                    case Opcode::Shld:
                    case Opcode::Shrd: {
                        requireInt(instr.type, "funnel shift");
                        size_t bits = bitWidth(instr.type);
                        UInt128 hi = unsignedOf(readOperand(frame, instr.operands[0], instr.type), instr.type);
                        UInt128 lo = unsignedOf(readOperand(frame, instr.operands[1], instr.type), instr.type);
                        Int128 n = readOperand(frame, instr.operands[2], Type::I64).asInt128();
                        if (n < 0 || static_cast<size_t>(n) >= bits) throw RuntimeError("shift amount out of range");
                        size_t s = static_cast<size_t>(n);
                        UInt128 r;
                        if (s == 0) {
                            r = (instr.opcode == Opcode::Shld) ? hi : lo;
                        } else if (instr.opcode == Opcode::Shld) {
                            r = (hi << s) | (lo >> (bits - s));
                        } else {
                            r = (lo >> s) | (hi << (bits - s));
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(maskToWidth(r, bits)));
                        break;
                    }
                    case Opcode::Bitselect: {
                        requireInt(instr.type, "bitselect");
                        UInt128 a = unsignedOf(readOperand(frame, instr.operands[0], instr.type), instr.type);
                        UInt128 b = unsignedOf(readOperand(frame, instr.operands[1], instr.type), instr.type);
                        UInt128 m = unsignedOf(readOperand(frame, instr.operands[2], instr.type), instr.type);
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>((a & m) | (b & ~m)));
                        break;
                    }
                    case Opcode::Memchr:
                    case Opcode::Memrchr: {
                        uint64_t buf = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        uint8_t c = static_cast<uint8_t>(readOperand(frame, instr.operands[2], instr.type).asInt128() & 0xFF);
                        checkRange(buf, len);
                        uint64_t found = 0;
                        bool ok = false;
                        if (instr.opcode == Opcode::Memchr) {
                            for (uint64_t i = 0; i < len; ++i) {
                                if (memory_[buf + i] == c) { found = buf + i; ok = true; break; }
                            }
                        } else {
                            for (uint64_t i = len; i-- > 0;) {
                                if (memory_[buf + i] == c) { found = buf + i; ok = true; break; }
                            }
                        }
                        frame.reg(instr.dest) = makePtr(ok ? found : 0);
                        break;
                    }
                    case Opcode::Memfind: {
                        uint64_t hay = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t hlen = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        uint64_t nee = readOperand(frame, instr.operands[2], Type::Ptr).bits.ptr;
                        uint64_t nlen = static_cast<uint64_t>(readOperand(frame, instr.operands[3], instr.type).asInt128());
                        checkRange(hay, hlen);
                        checkRange(nee, nlen);
                        uint64_t found = 0;
                        bool ok = false;
                        if (nlen <= hlen) {
                            for (uint64_t i = 0; i + nlen <= hlen; ++i) {
                                if (std::memcmp(&memory_[0] + hay + i, &memory_[0] + nee, nlen) == 0) {
                                    found = hay + i;
                                    ok = true;
                                    break;
                                }
                            }
                        }
                        frame.reg(instr.dest) = makePtr(ok ? found : 0);
                        break;
                    }
                    case Opcode::Memswap: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t b = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[2], instr.type).asInt128());
                        checkRange(a, len);
                        checkRange(b, len);
                        for (uint64_t i = 0; i < len; ++i) {
                            uint8_t t = memory_[a + i];
                            memory_[a + i] = memory_[b + i];
                            memory_[b + i] = t;
                        }
                        break;
                    }
                    case Opcode::Memrev: {
                        uint64_t buf = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        checkRange(buf, len);
                        for (uint64_t i = 0; i * 2 + 1 < len; ++i) {
                            uint8_t t = memory_[buf + i];
                            memory_[buf + i] = memory_[buf + len - 1 - i];
                            memory_[buf + len - 1 - i] = t;
                        }
                        break;
                    }
                    case Opcode::Memzero: {
                        uint64_t buf = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        memSetOp(buf, 0, len);
                        break;
                    }
                    case Opcode::Memeq: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t b = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[2], instr.type).asInt128());
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, memCmpOp(a, b, len) == 0 ? 1 : 0);
                        break;
                    }
                    case Opcode::Memcount: {
                        uint64_t buf = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        uint8_t c = static_cast<uint8_t>(readOperand(frame, instr.operands[2], instr.type).asInt128() & 0xFF);
                        checkRange(buf, len);
                        uint64_t n = 0;
                        for (uint64_t i = 0; i < len; ++i) {
                            if (memory_[buf + i] == c) ++n;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(n));
                        break;
                    }
                    case Opcode::Strlen:
                    case Opcode::Strnlen: {
                        uint64_t s = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t cap = memory_.size();
                        if (instr.opcode == Opcode::Strnlen) {
                            uint64_t m = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                            if (s + m < cap) cap = s + m;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(stringLength(s, cap)));
                        break;
                    }
                    case Opcode::Strcmp:
                    case Opcode::Strncmp:
                    case Opcode::Strcasecmp: {
                        uint64_t a = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t b = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        uint64_t n = (instr.opcode == Opcode::Strncmp)
                                         ? static_cast<uint64_t>(readOperand(frame, instr.operands[2], instr.type).asInt128())
                                         : memory_.size();
                        bool fold = instr.opcode == Opcode::Strcasecmp;
                        int r = 0;
                        for (uint64_t i = 0; i < n; ++i) {
                            checkRange(a + i, 1);
                            checkRange(b + i, 1);
                            int ca = memory_[a + i];
                            int cb = memory_[b + i];
                            if (fold) {
                                if (ca >= 'A' && ca <= 'Z') ca += 32;
                                if (cb >= 'A' && cb <= 'Z') cb += 32;
                            }
                            if (ca != cb) { r = ca < cb ? -1 : 1; break; }
                            if (ca == 0) break;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, r);
                        break;
                    }
                    case Opcode::Strcpy:
                    case Opcode::Strncpy:
                    case Opcode::Strcat:
                    case Opcode::Strncat: {
                        uint64_t dst = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t src = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        bool bounded = instr.opcode == Opcode::Strncpy || instr.opcode == Opcode::Strncat;
                        uint64_t n = bounded
                                         ? static_cast<uint64_t>(readOperand(frame, instr.operands[2], instr.type).asInt128())
                                         : memory_.size();
                        bool append = instr.opcode == Opcode::Strcat || instr.opcode == Opcode::Strncat;
                        uint64_t at = dst + (append ? stringLength(dst, memory_.size()) : 0);
                        uint64_t i = 0;
                        for (; i < n; ++i) {
                            checkRange(src + i, 1);
                            checkRange(at + i, 1);
                            uint8_t c = memory_[src + i];
                            memory_[at + i] = c;
                            if (c == 0) break;
                        }
                        if (i == n) {
                            checkRange(at + i, 1);
                            memory_[at + i] = 0;
                        }
                        break;
                    }
                    case Opcode::Strchr:
                    case Opcode::Strrchr: {
                        uint64_t s = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint8_t c = static_cast<uint8_t>(readOperand(frame, instr.operands[1], instr.type).asInt128() & 0xFF);
                        uint64_t len = stringLength(s, memory_.size());
                        uint64_t found = 0;
                        bool ok = false;
                        for (uint64_t i = 0; i <= len; ++i) {
                            if (memory_[s + i] == c) {
                                found = s + i;
                                ok = true;
                                if (instr.opcode == Opcode::Strchr) break;
                            }
                        }
                        frame.reg(instr.dest) = makePtr(ok ? found : 0);
                        break;
                    }
                    case Opcode::Strstr: {
                        uint64_t h = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t nd = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        uint64_t hlen = stringLength(h, memory_.size());
                        uint64_t nlen = stringLength(nd, memory_.size());
                        uint64_t found = 0;
                        bool ok = false;
                        if (nlen == 0) {
                            found = h;
                            ok = true;
                        } else if (nlen <= hlen) {
                            for (uint64_t i = 0; i + nlen <= hlen; ++i) {
                                if (std::memcmp(&memory_[0] + h + i, &memory_[0] + nd, nlen) == 0) {
                                    found = h + i;
                                    ok = true;
                                    break;
                                }
                            }
                        }
                        frame.reg(instr.dest) = makePtr(ok ? found : 0);
                        break;
                    }
                    case Opcode::Strspn:
                    case Opcode::Strcspn:
                    case Opcode::Strpbrk: {
                        uint64_t s = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t set = readOperand(frame, instr.operands[1], Type::Ptr).bits.ptr;
                        uint64_t slen = stringLength(s, memory_.size());
                        uint64_t setlen = stringLength(set, memory_.size());
                        uint64_t i = 0;
                        bool found = false;
                        for (; i < slen; ++i) {
                            bool inSet = false;
                            for (uint64_t j = 0; j < setlen; ++j) {
                                if (memory_[s + i] == memory_[set + j]) { inSet = true; break; }
                            }
                            if (instr.opcode == Opcode::Strspn) {
                                if (!inSet) break;
                            } else {
                                if (inSet) { found = true; break; }
                            }
                        }
                        if (instr.opcode == Opcode::Strpbrk) {
                            frame.reg(instr.dest) = makePtr(found ? s + i : 0);
                        } else {
                            frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(i));
                        }
                        break;
                    }
                    case Opcode::Strrev:
                    case Opcode::Strupper:
                    case Opcode::Strlower: {
                        uint64_t s = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = stringLength(s, memory_.size());
                        if (instr.opcode == Opcode::Strrev) {
                            for (uint64_t i = 0; i * 2 + 1 < len; ++i) {
                                uint8_t t = memory_[s + i];
                                memory_[s + i] = memory_[s + len - 1 - i];
                                memory_[s + len - 1 - i] = t;
                            }
                        } else {
                            for (uint64_t i = 0; i < len; ++i) {
                                uint8_t c = memory_[s + i];
                                if (instr.opcode == Opcode::Strupper) {
                                    if (c >= 'a' && c <= 'z') memory_[s + i] = static_cast<uint8_t>(c - 32);
                                } else {
                                    if (c >= 'A' && c <= 'Z') memory_[s + i] = static_cast<uint8_t>(c + 32);
                                }
                            }
                        }
                        break;
                    }
                    case Opcode::Utf8Decode: {
                        uint64_t p = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        checkRange(p, 1);
                        uint32_t cp = 0;
                        uint8_t c0 = memory_[p];
                        int extra = 0;
                        if (c0 < 0x80) { cp = c0; }
                        else if ((c0 & 0xE0) == 0xC0) { cp = c0 & 0x1F; extra = 1; }
                        else if ((c0 & 0xF0) == 0xE0) { cp = c0 & 0x0F; extra = 2; }
                        else if ((c0 & 0xF8) == 0xF0) { cp = c0 & 0x07; extra = 3; }
                        else throw RuntimeError("invalid utf-8 lead byte");
                        checkRange(p, static_cast<uint64_t>(extra) + 1);
                        for (int k = 1; k <= extra; ++k) {
                            uint8_t cc = memory_[p + k];
                            if ((cc & 0xC0) != 0x80) throw RuntimeError("invalid utf-8 continuation byte");
                            cp = (cp << 6) | (cc & 0x3F);
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(cp));
                        break;
                    }
                    case Opcode::Utf8Encode: {
                        uint64_t p = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint32_t cp = static_cast<uint32_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        uint8_t buf[4];
                        int n = 0;
                        if (cp < 0x80) { buf[0] = static_cast<uint8_t>(cp); n = 1; }
                        else if (cp < 0x800) {
                            buf[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
                            buf[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                            n = 2;
                        } else if (cp < 0x10000) {
                            buf[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
                            buf[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                            buf[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                            n = 3;
                        } else {
                            buf[0] = static_cast<uint8_t>(0xF0 | (cp >> 18));
                            buf[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
                            buf[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                            buf[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                            n = 4;
                        }
                        checkRange(p, static_cast<uint64_t>(n));
                        for (int k = 0; k < n; ++k) memory_[p + k] = buf[k];
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, n);
                        break;
                    }
                    case Opcode::Utf8Len: {
                        uint64_t p = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = stringLength(p, memory_.size());
                        uint64_t n = 0;
                        for (uint64_t i = 0; i < len; ++i) {
                            if ((memory_[p + i] & 0xC0) != 0x80) ++n;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, static_cast<Int128>(n));
                        break;
                    }
                    case Opcode::Utf8Valid: {
                        uint64_t p = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        uint64_t len = static_cast<uint64_t>(readOperand(frame, instr.operands[1], instr.type).asInt128());
                        checkRange(p, len);
                        bool ok = true;
                        uint64_t i = 0;
                        while (i < len) {
                            uint8_t c0 = memory_[p + i];
                            int extra = 0;
                            if (c0 < 0x80) extra = 0;
                            else if ((c0 & 0xE0) == 0xC0) extra = 1;
                            else if ((c0 & 0xF0) == 0xE0) extra = 2;
                            else if ((c0 & 0xF8) == 0xF0) extra = 3;
                            else { ok = false; break; }
                            if (i + static_cast<uint64_t>(extra) >= len) { ok = false; break; }
                            for (int k = 1; k <= extra; ++k) {
                                if ((memory_[p + i + k] & 0xC0) != 0x80) { ok = false; break; }
                            }
                            if (!ok) break;
                            i += static_cast<uint64_t>(extra) + 1;
                        }
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, ok ? 1 : 0);
                        break;
                    }
                    case Opcode::Assert: {
                        Value c = readOperand(frame, instr.operands[0], instr.type);
                        if (c.asInt128() == 0) throw RuntimeError("assertion failed");
                        break;
                    }
                    case Opcode::Expect:
                    case Opcode::Freeze: {
                        frame.reg(instr.dest) = readOperand(frame, instr.operands[0], instr.type);
                        break;
                    }
                    case Opcode::Poison: {
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, 0);
                        break;
                    }
                    case Opcode::Breakpoint:
                    case Opcode::Assume:
                    case Opcode::Prefetch:
                    case Opcode::Hint:
                    case Opcode::Dbgloc:
                    case Opcode::Trace:
                    case Opcode::Probe:
                    case Opcode::Noopt:
                    case Opcode::Sideeffect:
                        break;
#define UASM_OP(name, mnemonic, ext, flags) case Opcode::name:
#include "uasm/opcodes_simd.def"
#undef UASM_OP
                    {
                        if (!vectorOpIsMemory(instr.opcode)) {
                            std::vector<Value> vargs;
                            for (size_t i = 0; i < instr.operands.size(); ++i) {
                                vargs.push_back(readOperand(frame, instr.operands[i],
                                                            instr.operands[i].kind == Operand::Register
                                                                ? instr.type
                                                                : instr.type2));
                            }
                            Value r = vectorCompute(instr.opcode, instr.type, instr.type2,
                                                    vargs.empty() ? 0 : &vargs[0], vargs.size());
                            if (instr.hasDest) frame.reg(instr.dest) = r;
                            break;
                        }
                        switch (instr.opcode) {
                    case Opcode::VLoad:
                    case Opcode::VLoadu: {
                        uint64_t addr = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        size_t nbytes = vectorBytes(instr.type);
                        checkRange(addr, nbytes);
                        Value v;
                        v.type = instr.type;
                        std::memcpy(v.bits.vec, &memory_[0] + addr, nbytes);
                        frame.reg(instr.dest) = v;
                        break;
                    }
                    case Opcode::VStore:
                    case Opcode::VStoreu: {
                        uint64_t addr = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Value v = readOperand(frame, instr.operands[1], instr.type);
                        size_t nbytes = vectorBytes(instr.type);
                        checkRange(addr, nbytes);
                        std::memcpy(&memory_[0] + addr, v.bits.vec, nbytes);
                        break;
                    }
                    case Opcode::VLoadsplat: {
                        uint64_t addr = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        size_t ls = sizeOfType(instr.type2);
                        checkRange(addr, ls);
                        Value lane;
                        lane.type = instr.type2;
                        std::memcpy(&lane.bits, &memory_[0] + addr, ls);
                        Value args[1];
                        args[0] = lane;
                        frame.reg(instr.dest) = vectorCompute(Opcode::VSplat, instr.type, instr.type2, args, 1);
                        break;
                    }
                    case Opcode::VGather: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Value idx = readOperand(frame, instr.operands[1], instr.type);
                        size_t n = vectorLaneCount(instr.type, instr.type2);
                        size_t ls = sizeOfType(instr.type2);
                        Value v;
                        v.type = instr.type;
                        for (size_t i = 0; i < n; ++i) {
                            Int128 k = vectorGetLane(idx, instr.type2, i).asInt128();
                            uint64_t a = base + static_cast<uint64_t>(k) * ls;
                            checkRange(a, ls);
                            Value lane;
                            lane.type = instr.type2;
                            std::memcpy(&lane.bits, &memory_[0] + a, ls);
                            vectorSetLane(v, instr.type2, i, lane);
                        }
                        frame.reg(instr.dest) = v;
                        break;
                    }
                    case Opcode::VScatter: {
                        uint64_t base = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Value idx = readOperand(frame, instr.operands[1], instr.type);
                        Value src = readOperand(frame, instr.operands[2], instr.type);
                        size_t n = vectorLaneCount(instr.type, instr.type2);
                        size_t ls = sizeOfType(instr.type2);
                        for (size_t i = 0; i < n; ++i) {
                            Int128 k = vectorGetLane(idx, instr.type2, i).asInt128();
                            uint64_t a = base + static_cast<uint64_t>(k) * ls;
                            checkRange(a, ls);
                            Value lane = vectorGetLane(src, instr.type2, i);
                            std::memcpy(&memory_[0] + a, &lane.bits, ls);
                        }
                        break;
                    }
                    case Opcode::VMaskload: {
                        uint64_t addr = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Value mask = readOperand(frame, instr.operands[1], instr.type);
                        size_t n = vectorLaneCount(instr.type, instr.type2);
                        size_t ls = sizeOfType(instr.type2);
                        Value v;
                        v.type = instr.type;
                        for (size_t i = 0; i < n; ++i) {
                            if (vectorGetLane(mask, instr.type2, i).asInt128() == 0) continue;
                            uint64_t a = addr + i * ls;
                            checkRange(a, ls);
                            Value lane;
                            lane.type = instr.type2;
                            std::memcpy(&lane.bits, &memory_[0] + a, ls);
                            vectorSetLane(v, instr.type2, i, lane);
                        }
                        frame.reg(instr.dest) = v;
                        break;
                    }
                    case Opcode::VMaskstore: {
                        uint64_t addr = readOperand(frame, instr.operands[0], Type::Ptr).bits.ptr;
                        Value mask = readOperand(frame, instr.operands[1], instr.type);
                        Value src = readOperand(frame, instr.operands[2], instr.type);
                        size_t n = vectorLaneCount(instr.type, instr.type2);
                        size_t ls = sizeOfType(instr.type2);
                        for (size_t i = 0; i < n; ++i) {
                            if (vectorGetLane(mask, instr.type2, i).asInt128() == 0) continue;
                            uint64_t a = addr + i * ls;
                            checkRange(a, ls);
                            Value lane = vectorGetLane(src, instr.type2, i);
                            std::memcpy(&memory_[0] + a, &lane.bits, ls);
                        }
                        break;
                    }
                            default: break;
                        }
                        break;
                    }
                    case Opcode::Syscall: {
                        int64_t sid = static_cast<int64_t>(readOperand(frame, instr.operands[0], Type::I64).asInt128());
                        int64_t a0 = instr.operands.size() > 1
                                         ? static_cast<int64_t>(readOperand(frame, instr.operands[1], Type::I64).asInt128())
                                         : 0;
                        int64_t a1 = instr.operands.size() > 2
                                         ? static_cast<int64_t>(readOperand(frame, instr.operands[2], Type::I64).asInt128())
                                         : 0;
                        int64_t a2 = instr.operands.size() > 3
                                         ? static_cast<int64_t>(readOperand(frame, instr.operands[3], Type::I64).asInt128())
                                         : 0;
                        int64_t result = platformSyscall(sid, a0, a1, a2, &memory_[0], memory_.size());
                        frame.reg(instr.dest) = Value::fromInt128(instr.type, result);
                        break;
                    }
                }
                if (jumped) break;
            }

            if (!jumped) {
                throw RuntimeError("fell off the end of block '" + block.label + "' in function '" + fn.name +
                                    "' without a ret");
            }
        }
    }

private:
    Value loadValue(uint64_t addr, Type::Value type) {
        size_t sz = sizeOfType(type);
        if (addr + sz > memory_.size()) throw RuntimeError("load out of bounds at address " + toString(addr));
        Value v;
        v.type = type;
        std::memcpy(&v.bits, &memory_[0] + addr, sz);
        return v;
    }

    void storeValue(uint64_t addr, const Value& value) {
        size_t sz = sizeOfType(value.type);
        if (addr + sz > memory_.size()) throw RuntimeError("store out of bounds at address " + toString(addr));
        std::memcpy(&memory_[0] + addr, &value.bits, sz);
    }

    void pushValue(const Value& value) {
        size_t sz = sizeOfType(value.type);
        if (sz > stackPointer_) throw RuntimeError("stack overflow");
        stackPointer_ -= sz;
        std::memcpy(&stack_[0] + stackPointer_, &value.bits, sz);
    }

    Value popValue(Type::Value type) {
        size_t sz = sizeOfType(type);
        if (stackPointer_ + sz > stack_.size()) throw RuntimeError("stack underflow");
        Value v;
        v.type = type;
        std::memcpy(&v.bits, &stack_[0] + stackPointer_, sz);
        stackPointer_ += sz;
        return v;
    }

    uint64_t stringLength(uint64_t addr, uint64_t cap) {
        if (cap > memory_.size()) cap = memory_.size();
        for (uint64_t i = addr; i < cap; ++i) {
            if (memory_[i] == 0) return i - addr;
        }
        throw RuntimeError("unterminated string at address " + toString(addr));
    }

    void freeAllocas(Frame& frame) {
        for (size_t i = frame.allocas.size(); i-- > 0;) heapFree(frame.allocas[i]);
        frame.allocas.clear();
    }

    void checkRange(uint64_t addr, uint64_t len) const {
        if (addr + len > memory_.size()) throw RuntimeError("memory operation out of bounds at address " + toString(addr));
    }

    void memRangeOp(Opcode::Value op, uint64_t dst, uint64_t src, uint64_t len) {
        checkRange(dst, len);
        checkRange(src, len);
        if (op == Opcode::MemCpy) {
            std::memcpy(&memory_[0] + dst, &memory_[0] + src, len);
        } else {
            std::memmove(&memory_[0] + dst, &memory_[0] + src, len);
        }
    }

    void memSetOp(uint64_t dst, uint8_t value, uint64_t len) {
        checkRange(dst, len);
        std::memset(&memory_[0] + dst, value, len);
    }

    int memCmpOp(uint64_t a, uint64_t b, uint64_t len) {
        checkRange(a, len);
        checkRange(b, len);
        return std::memcmp(&memory_[0] + a, &memory_[0] + b, len);
    }

    uint64_t heapAlloc(uint64_t size) {
        if (size == 0) size = 1;
        for (size_t i = 0; i < heapBlocks_.size(); ++i) {
            if (heapBlocks_[i].free && heapBlocks_[i].size >= size) {
                heapBlocks_[i].free = false;
                if (heapBlocks_[i].size > size) {
                    HeapBlock leftover;
                    leftover.offset = heapBlocks_[i].offset + size;
                    leftover.size = heapBlocks_[i].size - size;
                    leftover.free = true;
                    heapBlocks_[i].size = size;
                    heapBlocks_.insert(heapBlocks_.begin() + i + 1, leftover);
                }
                return heapBlocks_[i].offset;
            }
        }
        if (heapBumpPtr_ + size > memory_.size()) throw RuntimeError("out of memory");
        HeapBlock nb;
        nb.offset = heapBumpPtr_;
        nb.size = size;
        nb.free = false;
        heapBlocks_.push_back(nb);
        heapBumpPtr_ += size;
        return nb.offset;
    }

    void heapFree(uint64_t offset) {
        for (size_t i = 0; i < heapBlocks_.size(); ++i) {
            if (heapBlocks_[i].offset == offset && !heapBlocks_[i].free) {
                heapBlocks_[i].free = true;
                return;
            }
        }
        throw RuntimeError("free of untracked pointer " + toString(offset));
    }

    uint64_t heapBlockSize(uint64_t offset) {
        for (size_t i = 0; i < heapBlocks_.size(); ++i) {
            if (heapBlocks_[i].offset == offset && !heapBlocks_[i].free) return heapBlocks_[i].size;
        }
        throw RuntimeError("operation on untracked pointer " + toString(offset));
    }

    uint64_t heapRealloc(uint64_t offset, uint64_t newSize) {
        uint64_t oldSize = heapBlockSize(offset);
        uint64_t newOffset = heapAlloc(newSize);
        uint64_t copyLen = oldSize < newSize ? oldSize : newSize;
        if (copyLen > 0) std::memcpy(&memory_[0] + newOffset, &memory_[0] + offset, copyLen);
        heapFree(offset);
        return newOffset;
    }

    static bool shouldBranch(Opcode::Value op, int flag) {
        switch (op) {
            case Opcode::Beq: return flag == 0;
            case Opcode::Bne: return flag != 0;
            case Opcode::Blt: return flag < 0;
            case Opcode::Bgt: return flag > 0;
            case Opcode::Ble: return flag <= 0;
            case Opcode::Bge: return flag >= 0;
            default: return false;
        }
    }

    static size_t resolveBlock(const NameIndexMap& blockIndex, const std::string& label,
                                const std::string& fnName) {
        NameIndexMap::const_iterator it = blockIndex.find(label);
        if (it == blockIndex.end()) {
            throw RuntimeError("branch to unknown block '" + label + "' in function '" + fnName + "'");
        }
        return it->second;
    }

    const Program& program_;
    NameIndexMap functionIndex_;
    std::vector<uint8_t> memory_;
    std::vector<uint8_t> stack_;
    size_t stackPointer_;
    std::vector<HeapBlock> heapBlocks_;
    uint64_t heapBumpPtr_;
};

}

Value run(const Program& program, const std::vector<Value>& args) {
    Interpreter interp(program);
    return interp.call(program.entryIndex, args);
}

}
