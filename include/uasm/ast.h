#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uasm/types.h"

namespace uasm {

namespace Opcode {
enum Value {
    Mov = 0,
    Add,
    Sub,
    Mul,
    Div,
    Cmp,
    Beq,
    Bne,
    Blt,
    Bgt,
    Ble,
    Bge,
    Jmp,
    Call,
    Ret,
    RetVoid,
    Load,
    Store,
    And,
    Or,
    Xor,
    Not,
    Shl,
    Shr,
    Mod,
    Neg,
    Push,
    Pop,
    Convert,
    Cast,

    Alloc,
    Free,
    Realloc,
    MemCpy,
    MemSet,
    MemMove,
    MemCmp,

    Sqrt,
    Cbrt,
    Floor,
    Ceil,
    Round,
    Trunc,
    Abs,
    Min,
    Max,
    Pow,
    Fma,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Atan2,
    Sinh,
    Cosh,
    Tanh,
    Log,
    Log2,
    Log10,
    Exp,
    Exp2,
    Hypot,
    Copysign,
    Fmod,

    Popcount,
    Clz,
    Ctz,
    Bswap,
    Rotl,
    Rotr,
    Bitset,
    Bitclear,
    Bittest,
    Parity,
    Ffs,
    Bitreverse
};
}

struct Operand {

    enum Kind { Register, ImmediateInt, ImmediateFloat, Label, Symbol };

    Kind kind;
    uint32_t reg;
    int64_t immInt;
    double immFloat;
    std::string name;

    Operand() : kind(Register), reg(0), immInt(0), immFloat(0.0) {}
};

struct Instruction {
    Opcode::Value opcode;
    Type::Value type;
    bool hasDest;
    uint32_t dest;
    std::vector<Operand> operands;
    int line;

    Instruction() : opcode(Opcode::Mov), type(Type::I32), hasDest(false), dest(0), line(0) {}
};

struct Block {
    std::string label;
    std::vector<Instruction> instructions;
};

struct Param {
    std::string name;
    Type::Value type;

    Param() : type(Type::I32) {}
};

struct Function {
    std::string name;
    std::string module;
    bool isExported;
    std::vector<Param> params;
    Type::Value returnType;
    std::vector<Block> blocks;

    Function() : isExported(false), returnType(Type::Void) {}
};

struct Module {
    std::string name;
    std::vector<Function> functions;
};

}
