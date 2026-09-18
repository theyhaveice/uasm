#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace uasm {

void setProcessArgs(int argc, char** argv);
int getProcessArgc();
std::string getProcessArgv(int index);

int64_t platformSyscall(int64_t id, int64_t a0, int64_t a1, int64_t a2, uint8_t* mem, uint64_t memSize);

}
