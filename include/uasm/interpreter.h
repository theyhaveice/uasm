#pragma once

#include "uasm/linker.h"
#include "uasm/value.h"

namespace uasm {

struct RuntimeError {
    std::string message;
    explicit RuntimeError(const std::string& m) : message(m) {}
};

Value run(const Program& program, const std::vector<Value>& args = std::vector<Value>());

}
