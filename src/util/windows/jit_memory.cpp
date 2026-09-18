#include "../jit_memory_platform.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdexcept>

namespace uasm {

void* platformAllocExecutableMemory(size_t size) {
    void* addr = ::VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) throw std::runtime_error("VirtualAlloc failed while allocating JIT memory");
    return addr;
}

void platformMakeExecutable(void* addr, size_t size) {
    DWORD oldProtect;
    if (!::VirtualProtect(addr, size, PAGE_EXECUTE_READ, &oldProtect)) {
        throw std::runtime_error("VirtualProtect failed while finalizing JIT memory");
    }
#if defined(__GNUC__) || defined(__clang__)
    char* begin = static_cast<char*>(addr);
    __builtin___clear_cache(begin, begin + size);
#endif
}

void platformFreeExecutableMemory(void* addr, size_t size) {
    ::VirtualFree(addr, size, MEM_RELEASE);
}

}

#endif
