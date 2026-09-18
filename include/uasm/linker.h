#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uasm/ast.h"
#include "uasm/object.h"

namespace uasm {

struct LinkError {
    std::string message;
    explicit LinkError(const std::string& m) : message(m) {}
};

struct Program {
    std::vector<Function> functions;
    uint32_t entryIndex;

    Program() : entryIndex(0) {}
};

Program link(const std::vector<Object>& objects);

}
