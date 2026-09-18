#include "host_target.h"

namespace uasm {

bool hasHostCodegenTarget() {
#if defined(__APPLE__) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

CodegenTarget hostCodegenTarget() {
#if defined(__APPLE__) && defined(__aarch64__)
    return CodegenTarget(TargetArch::Arm64, TargetOs::MacOS);
#else
    throw CodegenError("this host's architecture/OS has no native codegen backend yet");
#endif
}

}
