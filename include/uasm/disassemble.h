#pragma once

#include <stdexcept>
#include <string>

#include "uasm/linker.h"

namespace uasm {

namespace DumpFormat {
enum Value { Uasm, Json, Yaml };
}

struct UnknownDumpFormat : std::runtime_error {
    explicit UnknownDumpFormat(const std::string& name)
        : std::runtime_error("unknown dump format '" + name + "' (expected uasm, json, or yaml)") {}
};

DumpFormat::Value dumpFormatFromName(const std::string& name);

std::string disassemble(const Program& program);

std::string toJson(const Program& program);

std::string toYaml(const Program& program);

std::string dump(const Program& program, DumpFormat::Value format);

}
