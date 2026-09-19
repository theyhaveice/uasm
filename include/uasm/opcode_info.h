#pragma once

#include <string>

#include "uasm/ast.h"
#include "uasm/extensions.h"

namespace uasm {

const char* opcodeName(Opcode::Value op);

unsigned opcodeFlags(Opcode::Value op);

Extension::Value opcodeExtension(Opcode::Value op);

bool opcodeFromName(const std::string& name, Opcode::Value& out);

inline bool takesTypeSuffix(Opcode::Value op) { return (opcodeFlags(op) & OpFlag::Suffix) != 0; }

inline bool takesSecondTypeSuffix(Opcode::Value op) { return (opcodeFlags(op) & OpFlag::Suffix2) != 0; }

inline bool hasDestOperand(Opcode::Value op) { return (opcodeFlags(op) & OpFlag::Dest) != 0; }

inline bool isTerminator(Opcode::Value op) { return (opcodeFlags(op) & OpFlag::Term) != 0; }

inline bool takesNoOperands(Opcode::Value op) { return (opcodeFlags(op) & OpFlag::NoOps) != 0; }

unsigned extensionOpcodeCount(Extension::Value e);

}
