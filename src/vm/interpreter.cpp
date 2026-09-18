#include "uasm/interpreter.h"

#include "syscall_platform.h"

#include "uasm/compat.h"

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

    Frame() : cmpFlag(0) {}

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
                switch (instr.opcode) {
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
                        return readOperand(frame, instr.operands[0], instr.type);
                    }
                    case Opcode::RetVoid: {
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
