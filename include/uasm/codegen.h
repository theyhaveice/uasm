#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "uasm/linker.h"

namespace uasm {

namespace TargetArch {
enum Value { X86, X86_64, Arm32, Arm64 };
}

namespace TargetOs {
enum Value { Windows, Linux, MacOS };
}

struct CodegenTarget {
    TargetArch::Value arch;
    TargetOs::Value os;

    CodegenTarget() : arch(TargetArch::Arm64), os(TargetOs::MacOS) {}
    CodegenTarget(TargetArch::Value a, TargetOs::Value o) : arch(a), os(o) {}
};

struct CodegenError : std::runtime_error {
    explicit CodegenError(const std::string& m) : std::runtime_error(m) {}
};

struct CompiledCode {
    std::vector<uint8_t> code;
    size_t entryOffset;

    CompiledCode() : entryOffset(0) {}
};

bool isCodegenSupported(const CodegenTarget& target);

CompiledCode compileProgram(const Program& program, const CodegenTarget& target);

CompiledCode compileProgramParallel(const Program& program, const CodegenTarget& target, int threads);

std::vector<uint8_t> wrapExecutable(const CodegenTarget& target, const CompiledCode& code);

bool executableNeedsExecBit(TargetOs::Value os);

const char* targetName(const CodegenTarget& target);

}
