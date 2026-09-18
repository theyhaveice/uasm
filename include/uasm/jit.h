#pragma once

#include <vector>

#include "uasm/linker.h"
#include "uasm/value.h"

namespace uasm {

bool jitAvailableOnHost();

Value runJit(const Program& program, const std::vector<Value>& args, int threads);

}
