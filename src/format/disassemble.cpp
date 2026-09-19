#include "uasm/disassemble.h"

#include "uasm/opcode_info.h"

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
    out << "    " << opcodeName(instr.opcode);
    if (takesTypeSuffix(instr.opcode)) out << "." << typeName(instr.type);
    if (takesSecondTypeSuffix(instr.opcode)) out << "." << typeName(instr.type2);
    out << " ";

    bool firstOperand = true;
    if (instr.opcode == Opcode::Call) {
        if (!instr.operands.empty()) {
            writeOperand(out, instr.operands[0]);
            firstOperand = false;
        }
        if (instr.hasDest) {
            if (!firstOperand) out << ", ";
            out << "r" << instr.dest;
            firstOperand = false;
        }
        for (size_t i = 1; i < instr.operands.size(); ++i) {
            if (!firstOperand) out << ", ";
            firstOperand = false;
            writeOperand(out, instr.operands[i]);
        }
        out << "\n";
        return;
    }

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
    out << indent << "  \"opcode\": \"" << opcodeName(instr.opcode) << "\",\n";
    if (takesTypeSuffix(instr.opcode)) {
        out << indent << "  \"type\": \"" << typeName(instr.type) << "\",\n";
    }
    if (takesSecondTypeSuffix(instr.opcode)) {
        out << indent << "  \"type2\": \"" << typeName(instr.type2) << "\",\n";
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
                out << "          - opcode: " << opcodeName(instr.opcode) << "\n";
                if (takesTypeSuffix(instr.opcode)) {
                    out << "            type: " << typeName(instr.type) << "\n";
                }
                if (takesSecondTypeSuffix(instr.opcode)) {
                    out << "            type2: " << typeName(instr.type2) << "\n";
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
