#include "uasm/interpreter.h"

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

const size_t kMemorySize = 1u << 20;

const size_t kStackSize = 1u << 16;

class Interpreter {
public:
    explicit Interpreter(const Program& program)
        : program_(program), memory_(kMemorySize, 0), stack_(kStackSize, 0), stackPointer_(kStackSize) {
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
};

}

Value run(const Program& program, const std::vector<Value>& args) {
    Interpreter interp(program);
    return interp.call(program.entryIndex, args);
}

}
