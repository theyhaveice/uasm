#include "uasm/jit.h"

#include "uasm/codegen.h"
#include "uasm/interpreter.h"

#include "../codegen/host_target.h"
#include "../util/jit_memory_platform.h"

#include <cstring>

namespace uasm {

bool jitAvailableOnHost() { return hasHostCodegenTarget(); }

namespace {
typedef int64_t (*EntryFn)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
}

Value runJit(const Program& program, const std::vector<Value>& args, int threads) {
    if (!hasHostCodegenTarget()) {
        throw RuntimeError("this host's architecture/OS has no native codegen backend yet (-j/--jit needs one)");
    }

    CodegenTarget target = hostCodegenTarget();
    CompiledCode code = compileProgramParallel(program, target, threads);

    void* mem = platformAllocExecutableMemory(code.code.size());
    std::memcpy(mem, &code.code[0], code.code.size());
    platformMakeExecutable(mem, code.code.size());

    int64_t argv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < args.size() && i < 8; ++i) {
        argv[i] = static_cast<int64_t>(args[i].asInt128());
    }

    EntryFn entry = reinterpret_cast<EntryFn>(reinterpret_cast<char*>(mem) + code.entryOffset);
    int64_t result = entry(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);

    platformFreeExecutableMemory(mem, code.code.size());

    Type::Value returnType = program.functions[program.entryIndex].returnType;
    if (returnType == Type::Void) {
        Value v;
        v.type = Type::Void;
        return v;
    }
    return Value::fromInt128(returnType, result);
}

}
