#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uasm/extensions.h"
#include "uasm/types.h"

namespace uasm {

namespace OpFlag {
enum Value {
    None = 0,
    Suffix = 1 << 0,
    Suffix2 = 1 << 1,
    Dest = 1 << 2,
    Term = 1 << 3,
    NoOps = 1 << 4
};
}

namespace Opcode {
enum Value {
#define UASM_OP(name, mnemonic, ext, flags) name,
#include "uasm/opcodes.def"
#undef UASM_OP
    OpcodeCount
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
    Type::Value type2;
    bool hasDest;
    uint32_t dest;
    std::vector<Operand> operands;
    int line;

    Instruction()
        : opcode(Opcode::Mov), type(Type::I32), type2(Type::I32), hasDest(false), dest(0), line(0) {}
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
