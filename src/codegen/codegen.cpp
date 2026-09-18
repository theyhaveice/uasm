#include "codegen_internal.h"

#include <cstring>

#if __cplusplus >= 201103L
#include <thread>
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

CompiledCode linkCompiledFunctions(const std::vector<CompiledFunction>& compiled, const Program& program) {
    NameIndexMap functionStart;
    CompiledCode result;
    for (size_t i = 0; i < compiled.size(); ++i) {
        functionStart[compiled[i].name] = result.code.size();
        result.code.insert(result.code.end(), compiled[i].code.begin(), compiled[i].code.end());
    }

    for (size_t i = 0; i < compiled.size(); ++i) {
        size_t base = functionStart[compiled[i].name];
        for (size_t f = 0; f < compiled[i].callFixups.size(); ++f) {
            const CallFixup& fixup = compiled[i].callFixups[f];
            NameIndexMap::const_iterator targetIt = functionStart.find(fixup.calleeName);
            if (targetIt == functionStart.end()) {
                throw CodegenError("call to unresolved function '" + fixup.calleeName + "'");
            }
            size_t absoluteOffset = base + fixup.byteOffset;
            int64_t delta = static_cast<int64_t>(targetIt->second) - static_cast<int64_t>(absoluteOffset);
            uint32_t word;
            std::memcpy(&word, &result.code[absoluteOffset], 4);
            int32_t imm26 = static_cast<int32_t>(delta / 4);
            word = (word & 0xFC000000u) | (static_cast<uint32_t>(imm26) & 0x3FFFFFFu);
            std::memcpy(&result.code[absoluteOffset], &word, 4);
        }
    }

    NameIndexMap::const_iterator entryIt = functionStart.find(program.functions[program.entryIndex].name);
    if (entryIt == functionStart.end()) throw CodegenError("entry function missing after codegen");
    result.entryOffset = entryIt->second;
    return result;
}

}

bool isCodegenSupported(const CodegenTarget& target) {
    return target.arch == TargetArch::Arm64 && target.os == TargetOs::MacOS;
}

const char* targetName(const CodegenTarget& target) {
    if (target.arch == TargetArch::Arm64 && target.os == TargetOs::MacOS) return "macos-arm64";
    return "unsupported-target";
}

CompiledCode compileProgram(const Program& program, const CodegenTarget& target) {
    if (!isCodegenSupported(target)) {
        throw CodegenError(std::string("no native codegen backend for target '") + targetName(target) +
                            "' yet (v0.4 only supports macos-arm64)");
    }

    std::vector<CompiledFunction> compiled;
    for (size_t i = 0; i < program.functions.size(); ++i) {
        compiled.push_back(arm64CompileFunction(program.functions[i]));
    }
    return linkCompiledFunctions(compiled, program);
}

CompiledCode compileProgramParallel(const Program& program, const CodegenTarget& target, int threads) {
    if (!isCodegenSupported(target)) {
        throw CodegenError(std::string("no native codegen backend for target '") + targetName(target) +
                            "' yet (v0.4 only supports macos-arm64)");
    }
    if (threads < 1) threads = 1;
    size_t n = program.functions.size();
    if (static_cast<size_t>(threads) > n && n > 0) threads = static_cast<int>(n);

    std::vector<CompiledFunction> compiled(n);
    std::vector<CodegenError*> errors(static_cast<size_t>(threads), 0);

#if __cplusplus >= 201103L
    if (threads > 1 && n > 0) {
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t) {
            workers.push_back(std::thread([&, t]() {
                for (size_t i = static_cast<size_t>(t); i < n; i += static_cast<size_t>(threads)) {
                    try {
                        compiled[i] = arm64CompileFunction(program.functions[i]);
                    } catch (const CodegenError& e) {
                        errors[static_cast<size_t>(t)] = new CodegenError(e);
                    }
                }
            }));
        }
        for (size_t t = 0; t < workers.size(); ++t) workers[t].join();
        for (size_t t = 0; t < errors.size(); ++t) {
            if (errors[t] != 0) {
                CodegenError err(*errors[t]);
                for (size_t j = 0; j < errors.size(); ++j) delete errors[j];
                throw err;
            }
        }
    } else {
        for (size_t i = 0; i < n; ++i) compiled[i] = arm64CompileFunction(program.functions[i]);
    }
#else
    for (size_t i = 0; i < n; ++i) compiled[i] = arm64CompileFunction(program.functions[i]);
#endif

    return linkCompiledFunctions(compiled, program);
}

std::vector<uint8_t> wrapExecutable(const CodegenTarget& target, const CompiledCode& code) {
    if (target.os == TargetOs::MacOS) return machoWrapExecutable(target.arch, code);
    throw CodegenError(std::string("no executable writer for target '") + targetName(target) + "' yet");
}

bool executableNeedsExecBit(TargetOs::Value os) { return os != TargetOs::Windows; }

}
