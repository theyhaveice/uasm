#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uasm/codegen.h"

namespace uasm {

struct CallFixup {
    size_t byteOffset;
    std::string calleeName;
};

struct CompiledFunction {
    std::string name;
    std::vector<uint8_t> code;
    std::vector<CallFixup> callFixups;
};

bool arm64IsSupported(const Function& fn);
CompiledFunction arm64CompileFunction(const Function& fn);

std::vector<uint8_t> machoWrapExecutable(TargetArch::Value arch, const CompiledCode& code);

}
