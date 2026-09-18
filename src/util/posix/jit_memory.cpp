#include "../jit_memory_platform.h"

#if !defined(_WIN32)

#include <sys/mman.h>

#include <stdexcept>
#include <string>

namespace uasm {

void* platformAllocExecutableMemory(size_t size) {
    void* addr = ::mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (addr == MAP_FAILED) throw std::runtime_error("mmap failed while allocating JIT memory");
    return addr;
}

void platformMakeExecutable(void* addr, size_t size) {
    if (::mprotect(addr, size, PROT_READ | PROT_EXEC) != 0) {
        throw std::runtime_error("mprotect failed while finalizing JIT memory");
    }
#if defined(__GNUC__) || defined(__clang__)
    char* begin = static_cast<char*>(addr);
    __builtin___clear_cache(begin, begin + size);
#endif
}

void platformFreeExecutableMemory(void* addr, size_t size) { ::munmap(addr, size); }

}

#endif
