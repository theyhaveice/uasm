#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uasm/linker.h"

namespace uasm {

const char kMagic[6] = {'U', 'A', 'S', 'M', '!', '0'};

const char kLibMagic[6] = {'U', 'A', 'S', 'M', '!', '1'};

const uint32_t kUoVersionSentinel = 0xFFFFFFFFu;

const uint32_t kUoVersion = 1;

struct SerializeError {
    std::string message;
    explicit SerializeError(const std::string& m) : message(m) {}
};

void writeUo(const Program& program, const std::string& path);

Program readUo(const std::string& path);

}
