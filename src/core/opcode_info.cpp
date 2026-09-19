#include "uasm/opcode_info.h"

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif

namespace uasm {

namespace {

struct OpcodeEntry {
    const char* name;
    unsigned flags;
    Extension::Value ext;
};

const OpcodeEntry kOpcodeTable[] = {
#define UASM_OP(name, mnemonic, ext, flags) {mnemonic, static_cast<unsigned>(flags), ext},
#include "uasm/opcodes.def"
#undef UASM_OP
};

const unsigned kOpcodeTableSize = sizeof(kOpcodeTable) / sizeof(kOpcodeTable[0]);

#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, Opcode::Value> NameMap;
#else
typedef std::map<std::string, Opcode::Value> NameMap;
#endif

const NameMap& nameMap() {
    static NameMap m;
    if (m.empty()) {
        for (unsigned i = 0; i < kOpcodeTableSize; ++i) {
            m.insert(std::make_pair(std::string(kOpcodeTable[i].name), static_cast<Opcode::Value>(i)));
        }
    }
    return m;
}

bool inRange(Opcode::Value op) {
    return static_cast<unsigned>(op) < kOpcodeTableSize;
}

}

const char* opcodeName(Opcode::Value op) {
    return inRange(op) ? kOpcodeTable[static_cast<unsigned>(op)].name : "<invalid>";
}

unsigned opcodeFlags(Opcode::Value op) {
    return inRange(op) ? kOpcodeTable[static_cast<unsigned>(op)].flags : 0u;
}

Extension::Value opcodeExtension(Opcode::Value op) {
    return inRange(op) ? kOpcodeTable[static_cast<unsigned>(op)].ext : Extension::Core;
}

bool opcodeFromName(const std::string& name, Opcode::Value& out) {
    NameMap::const_iterator it = nameMap().find(name);
    if (it == nameMap().end()) return false;
    out = it->second;
    return true;
}

unsigned extensionOpcodeCount(Extension::Value e) {
    unsigned n = 0;
    for (unsigned i = 0; i < kOpcodeTableSize; ++i) {
        if (kOpcodeTable[i].ext == e) ++n;
    }
    return n;
}

}
