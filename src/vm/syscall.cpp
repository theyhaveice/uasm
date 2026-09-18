#include "syscall_platform.h"

#include <vector>

namespace uasm {

namespace {
std::vector<std::string> g_processArgs;
}

void setProcessArgs(int argc, char** argv) {
    g_processArgs.clear();
    for (int i = 0; i < argc; ++i) g_processArgs.push_back(argv[i]);
}

int getProcessArgc() { return static_cast<int>(g_processArgs.size()); }

std::string getProcessArgv(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_processArgs.size()) return std::string();
    return g_processArgs[static_cast<size_t>(index)];
}

}
