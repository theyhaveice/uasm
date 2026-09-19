#pragma once

#include <string>

#include "uasm/ast.h"
#include "uasm/extensions.h"

namespace uasm {

struct ParseError {
    std::string message;
    int line;
    ParseError(const std::string& m, int l) : message(m), line(l) {}
};

Module parseModule(const std::string& source, const std::string& filename);

Module parseModule(const std::string& source, const std::string& filename, const ExtensionSet& enabled);

}
