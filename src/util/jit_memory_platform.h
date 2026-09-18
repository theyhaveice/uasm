#pragma once

#include <cstddef>
#include <cstdint>

namespace uasm {

void* platformAllocExecutableMemory(size_t size);
void platformMakeExecutable(void* addr, size_t size);
void platformFreeExecutableMemory(void* addr, size_t size);

}
