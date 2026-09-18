#include "uasm/disassemble.h"

#include <cctype>
#include <sstream>

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif

namespace uasm {

namespace {

#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, std::vector<size_t> > ModuleFunctionMap;
#else
typedef std::map<std::string, std::vector<size_t> > ModuleFunctionMap;
#endif

std::string toLower(std::string s) {
    for (std::string::size_type i = 0; i < s.size(); ++i) {
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return s;
}

std::string toRegisterString(uint32_t reg) {
    std::ostringstream ss;
    ss << "r" << reg;
    return ss.str();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::string::size_type i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

const char* operandKindName(Operand::Kind kind) {
    switch (kind) {
        case Operand::Register: return "register";
        case Operand::ImmediateInt: return "immediate_int";
        case Operand::ImmediateFloat: return "immediate_float";
        case Operand::Symbol: return "symbol";
        case Operand::Label: return "label";
    }
    return "unknown";
}

const char* mnemonic(Opcode::Value op) {
    switch (op) {
        case Opcode::Mov: return "mov";
        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::Div: return "div";
        case Opcode::Cmp: return "cmp";
        case Opcode::Beq: return "beq";
        case Opcode::Bne: return "bne";
        case Opcode::Blt: return "blt";
        case Opcode::Bgt: return "bgt";
        case Opcode::Ble: return "ble";
        case Opcode::Bge: return "bge";
        case Opcode::Jmp: return "jmp";
        case Opcode::Call: return "call";
        case Opcode::Ret: return "ret";
        case Opcode::RetVoid: return "ret";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::And: return "and";
        case Opcode::Or: return "or";
        case Opcode::Xor: return "xor";
        case Opcode::Not: return "not";
        case Opcode::Shl: return "shl";
        case Opcode::Shr: return "shr";
        case Opcode::Mod: return "mod";
        case Opcode::Neg: return "neg";
        case Opcode::Push: return "push";
        case Opcode::Pop: return "pop";
        case Opcode::Convert: return "convert";
        case Opcode::Cast: return "cast";
        case Opcode::Alloc: return "alloc";
        case Opcode::Free: return "free";
        case Opcode::Realloc: return "realloc";
        case Opcode::MemCpy: return "memcpy";
        case Opcode::MemSet: return "memset";
        case Opcode::MemMove: return "memmove";
        case Opcode::MemCmp: return "memcmp";
        case Opcode::Sqrt: return "sqrt";
        case Opcode::Cbrt: return "cbrt";
        case Opcode::Floor: return "floor";
        case Opcode::Ceil: return "ceil";
        case Opcode::Round: return "round";
        case Opcode::Trunc: return "trunc";
        case Opcode::Abs: return "abs";
        case Opcode::Min: return "min";
        case Opcode::Max: return "max";
        case Opcode::Pow: return "pow";
        case Opcode::Fma: return "fma";
        case Opcode::Sin: return "sin";
        case Opcode::Cos: return "cos";
        case Opcode::Tan: return "tan";
        case Opcode::Asin: return "asin";
        case Opcode::Acos: return "acos";
        case Opcode::Atan: return "atan";
        case Opcode::Atan2: return "atan2";
        case Opcode::Sinh: return "sinh";
        case Opcode::Cosh: return "cosh";
        case Opcode::Tanh: return "tanh";
        case Opcode::Log: return "log";
        case Opcode::Log2: return "log2";
        case Opcode::Log10: return "log10";
        case Opcode::Exp: return "exp";
        case Opcode::Exp2: return "exp2";
        case Opcode::Hypot: return "hypot";
        case Opcode::Copysign: return "copysign";
        case Opcode::Fmod: return "fmod";
        case Opcode::Popcount: return "popcount";
        case Opcode::Clz: return "clz";
        case Opcode::Ctz: return "ctz";
        case Opcode::Bswap: return "bswap";
        case Opcode::Rotl: return "rotl";
        case Opcode::Rotr: return "rotr";
        case Opcode::Bitset: return "bitset";
        case Opcode::Bitclear: return "bitclear";
        case Opcode::Bittest: return "bittest";
        case Opcode::Parity: return "parity";
        case Opcode::Ffs: return "ffs";
        case Opcode::Bitreverse: return "bitreverse";
    }
    return "?";
}

bool hasTypeSuffix(Opcode::Value op) {
    switch (op) {
        case Opcode::Mov:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Cmp:
        case Opcode::Ret:
        case Opcode::Load:
        case Opcode::Store:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Not:
        case Opcode::Shl:
        case Opcode::Shr:
        case Opcode::Mod:
        case Opcode::Neg:
        case Opcode::Push:
        case Opcode::Pop:
        case Opcode::Convert:
        case Opcode::Cast:
        case Opcode::Alloc:
        case Opcode::Free:
        case Opcode::Realloc:
        case Opcode::MemCpy:
        case Opcode::MemSet:
        case Opcode::MemMove:
        case Opcode::MemCmp:
        case Opcode::Sqrt:
        case Opcode::Cbrt:
        case Opcode::Floor:
        case Opcode::Ceil:
        case Opcode::Round:
        case Opcode::Trunc:
        case Opcode::Abs:
        case Opcode::Min:
        case Opcode::Max:
        case Opcode::Pow:
        case Opcode::Fma:
        case Opcode::Sin:
        case Opcode::Cos:
        case Opcode::Tan:
        case Opcode::Asin:
        case Opcode::Acos:
        case Opcode::Atan:
        case Opcode::Atan2:
        case Opcode::Sinh:
        case Opcode::Cosh:
        case Opcode::Tanh:
        case Opcode::Log:
        case Opcode::Log2:
        case Opcode::Log10:
        case Opcode::Exp:
        case Opcode::Exp2:
        case Opcode::Hypot:
        case Opcode::Copysign:
        case Opcode::Fmod:
        case Opcode::Popcount:
        case Opcode::Clz:
        case Opcode::Ctz:
        case Opcode::Bswap:
        case Opcode::Rotl:
        case Opcode::Rotr:
        case Opcode::Bitset:
        case Opcode::Bitclear:
        case Opcode::Bittest:
        case Opcode::Parity:
        case Opcode::Ffs:
        case Opcode::Bitreverse:
            return true;
        default:
            return false;
    }
}

void writeOperand(std::ostringstream& out, const Operand& op) {
    switch (op.kind) {
        case Operand::Register:
            out << "r" << op.reg;
            break;
        case Operand::ImmediateInt:
            out << op.immInt;
            break;
        case Operand::ImmediateFloat:
            out << op.immFloat;
            break;
        case Operand::Symbol:
        case Operand::Label:
            out << op.name;
            break;
    }
}

void writeInstruction(std::ostringstream& out, const Instruction& instr) {
    out << "    " << mnemonic(instr.opcode);
    if (hasTypeSuffix(instr.opcode)) out << "." << typeName(instr.type);
    out << " ";

    bool firstOperand = true;
    if (instr.hasDest) {
        out << "r" << instr.dest;
        firstOperand = false;
    }
    for (size_t i = 0; i < instr.operands.size(); ++i) {
        if (!firstOperand) out << ", ";
        firstOperand = false;
        writeOperand(out, instr.operands[i]);
    }
    out << "\n";
}

}

std::string disassemble(const Program& program) {
    std::ostringstream out;

    std::vector<std::string> moduleOrder;
    ModuleFunctionMap moduleFunctions;
    for (size_t i = 0; i < program.functions.size(); ++i) {
        const std::string& mod = program.functions[i].module;
        if (moduleFunctions.find(mod) == moduleFunctions.end()) moduleOrder.push_back(mod);
        moduleFunctions[mod].push_back(i);
    }

    for (size_t m = 0; m < moduleOrder.size(); ++m) {
        const std::string& modName = moduleOrder[m];
        out << "module " << modName << "\n\n";

        const std::vector<size_t>& indices = moduleFunctions[modName];
        for (size_t k = 0; k < indices.size(); ++k) {
            const Function& fn = program.functions[indices[k]];
            out << (fn.isExported ? "export " : "") << "func " << fn.name << "(";
            for (size_t p = 0; p < fn.params.size(); ++p) {
                if (p != 0) out << ", ";
                out << "p" << p << ": " << typeName(fn.params[p].type);
            }
            out << ") -> " << typeName(fn.returnType) << " {\n";

            for (size_t b = 0; b < fn.blocks.size(); ++b) {
                const Block& block = fn.blocks[b];
                out << block.label << ":\n";
                for (size_t ii = 0; ii < block.instructions.size(); ++ii) writeInstruction(out, block.instructions[ii]);
            }
            out << "}\n\n";
        }
    }

    return out.str();
}

namespace {

void writeOperandJson(std::ostringstream& out, const Operand& op) {
    out << "{\"kind\": \"" << operandKindName(op.kind) << "\"";
    switch (op.kind) {
        case Operand::Register:
            out << ", \"reg\": " << op.reg;
            break;
        case Operand::ImmediateInt:
            out << ", \"value\": " << op.immInt;
            break;
        case Operand::ImmediateFloat:
            out << ", \"value\": " << op.immFloat;
            break;
        case Operand::Symbol:
        case Operand::Label:
            out << ", \"name\": \"" << jsonEscape(op.name) << "\"";
            break;
    }
    out << "}";
}

void writeInstructionJson(std::ostringstream& out, const Instruction& instr, const std::string& indent) {
    out << indent << "{\n";
    out << indent << "  \"opcode\": \"" << mnemonic(instr.opcode) << "\",\n";
    if (hasTypeSuffix(instr.opcode)) {
        out << indent << "  \"type\": \"" << typeName(instr.type) << "\",\n";
    }
    out << indent << "  \"dest\": " << (instr.hasDest ? ("\"" + toRegisterString(instr.dest) + "\"") : "null")
        << ",\n";
    out << indent << "  \"operands\": [";
    for (size_t i = 0; i < instr.operands.size(); ++i) {
        if (i != 0) out << ", ";
        writeOperandJson(out, instr.operands[i]);
    }
    out << "]\n";
    out << indent << "}";
}

}

std::string toJson(const Program& program) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"entry_index\": " << program.entryIndex << ",\n";
    out << "  \"functions\": [\n";
    for (size_t i = 0; i < program.functions.size(); ++i) {
        const Function& fn = program.functions[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(fn.name) << "\",\n";
        out << "      \"module\": \"" << jsonEscape(fn.module) << "\",\n";
        out << "      \"exported\": " << (fn.isExported ? "true" : "false") << ",\n";
        out << "      \"return_type\": \"" << typeName(fn.returnType) << "\",\n";
        out << "      \"params\": [";
        for (size_t p = 0; p < fn.params.size(); ++p) {
            if (p != 0) out << ", ";
            out << "{\"index\": " << p << ", \"type\": \"" << typeName(fn.params[p].type) << "\"}";
        }
        out << "],\n";
        out << "      \"blocks\": [\n";
        for (size_t b = 0; b < fn.blocks.size(); ++b) {
            const Block& block = fn.blocks[b];
            out << "        {\n";
            out << "          \"label\": \"" << jsonEscape(block.label) << "\",\n";
            out << "          \"instructions\": [\n";
            for (size_t k = 0; k < block.instructions.size(); ++k) {
                writeInstructionJson(out, block.instructions[k], "            ");
                out << (k + 1 < block.instructions.size() ? ",\n" : "\n");
            }
            out << "          ]\n";
            out << "        }" << (b + 1 < fn.blocks.size() ? ",\n" : "\n");
        }
        out << "      ]\n";
        out << "    }" << (i + 1 < program.functions.size() ? ",\n" : "\n");
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

namespace {

std::string operandYamlInline(const Operand& op) {
    std::ostringstream out;
    out << "{kind: " << operandKindName(op.kind);
    switch (op.kind) {
        case Operand::Register:
            out << ", reg: " << op.reg;
            break;
        case Operand::ImmediateInt:
            out << ", value: " << op.immInt;
            break;
        case Operand::ImmediateFloat:
            out << ", value: " << op.immFloat;
            break;
        case Operand::Symbol:
        case Operand::Label:
            out << ", name: " << op.name;
            break;
    }
    out << "}";
    return out.str();
}

}

std::string toYaml(const Program& program) {
    std::ostringstream out;
    out << "entry_index: " << program.entryIndex << "\n";
    out << "functions:\n";
    for (size_t fi = 0; fi < program.functions.size(); ++fi) {
        const Function& fn = program.functions[fi];
        out << "  - name: " << fn.name << "\n";
        out << "    module: " << fn.module << "\n";
        out << "    exported: " << (fn.isExported ? "true" : "false") << "\n";
        out << "    return_type: " << typeName(fn.returnType) << "\n";
        out << "    params:";
        if (fn.params.empty()) {
            out << " []\n";
        } else {
            out << "\n";
            for (size_t p = 0; p < fn.params.size(); ++p) {
                out << "      - {index: " << p << ", type: " << typeName(fn.params[p].type) << "}\n";
            }
        }
        out << "    blocks:\n";
        for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
            const Block& block = fn.blocks[bi];
            out << "      - label: " << block.label << "\n";
            out << "        instructions:";
            if (block.instructions.empty()) {
                out << " []\n";
                continue;
            }
            out << "\n";
            for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
                const Instruction& instr = block.instructions[ii];
                out << "          - opcode: " << mnemonic(instr.opcode) << "\n";
                if (hasTypeSuffix(instr.opcode)) {
                    out << "            type: " << typeName(instr.type) << "\n";
                }
                out << "            dest: " << (instr.hasDest ? toRegisterString(instr.dest) : std::string("null"))
                    << "\n";
                out << "            operands:";
                if (instr.operands.empty()) {
                    out << " []\n";
                    continue;
                }
                out << "\n";
                for (size_t oi = 0; oi < instr.operands.size(); ++oi) {
                    out << "              - " << operandYamlInline(instr.operands[oi]) << "\n";
                }
            }
        }
    }
    return out.str();
}

DumpFormat::Value dumpFormatFromName(const std::string& name) {
    std::string lower = toLower(name);
    if (lower == "uasm") return DumpFormat::Uasm;
    if (lower == "json") return DumpFormat::Json;
    if (lower == "yaml" || lower == "yml") return DumpFormat::Yaml;
    throw UnknownDumpFormat(name);
}

std::string dump(const Program& program, DumpFormat::Value format) {
    switch (format) {
        case DumpFormat::Uasm: return disassemble(program);
        case DumpFormat::Json: return toJson(program);
        case DumpFormat::Yaml: return toYaml(program);
    }
    return disassemble(program);
}

}
