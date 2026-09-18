#pragma once

#if __cplusplus >= 201103L
#include <utility>
#define UASM_MOVE(x) std::move(x)
#else
#define UASM_MOVE(x) (x)
#endif
