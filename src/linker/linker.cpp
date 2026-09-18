#include "uasm/linker.h"

#if __cplusplus >= 201103L
#include <unordered_map>
#include <unordered_set>
#else
#include <map>
#include <set>
#endif

namespace uasm {

namespace {
#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, size_t> FunctionIndexMap;
typedef std::unordered_set<std::string> NameSet;
#else
typedef std::map<std::string, size_t> FunctionIndexMap;
typedef std::set<std::string> NameSet;
#endif
}

Program link(const std::vector<Object>& objects) {
    Program program;
    FunctionIndexMap functionIndex;
    NameSet exported;

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const Object& obj = objects[oi];
        for (size_t fi = 0; fi < obj.functions.size(); ++fi) {
            const Function& fn = obj.functions[fi];
            if (functionIndex.count(fn.name)) {
                throw LinkError("duplicate function definition: '" + fn.name + "'");
            }
            if (fn.isExported) {
                if (exported.count(fn.name)) {
                    throw LinkError("duplicate export: '" + fn.name + "'");
                }
                exported.insert(fn.name);
            }
            functionIndex[fn.name] = program.functions.size();
            program.functions.push_back(fn);
        }
    }

    for (size_t fi = 0; fi < program.functions.size(); ++fi) {
        const Function& fn = program.functions[fi];
        for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
            const Block& block = fn.blocks[bi];
            for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
                const Instruction& instr = block.instructions[ii];
                if (instr.opcode == Opcode::Call) {
                    const std::string& target = instr.operands[0].name;
                    if (!functionIndex.count(target)) {
                        throw LinkError("unresolved call to '" + target + "' in function '" + fn.name + "'");
                    }
                }
            }
        }
    }

    FunctionIndexMap::const_iterator mainIt = functionIndex.find("main");
    if (mainIt == functionIndex.end()) {
        throw LinkError("no exported 'main' function found");
    }
    if (!program.functions[mainIt->second].isExported) {
        throw LinkError("'main' is defined but not exported");
    }
    program.entryIndex = static_cast<uint32_t>(mainIt->second);

    return program;
}

}
