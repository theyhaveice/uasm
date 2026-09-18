#pragma once

#include "uasm/codegen.h"

namespace uasm {

bool hasHostCodegenTarget();
CodegenTarget hostCodegenTarget();

}
