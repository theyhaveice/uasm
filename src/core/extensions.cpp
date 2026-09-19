#include "uasm/extensions.h"

namespace uasm {

namespace {

const char* const kExtensionNames[] = {
    "core",    "simd128", "simd256",  "simd512", "atomic", "thread", "coroutine",
    "exception", "float16", "decimal", "fixed",  "bigint", "crypto", "crc",
    "bitfield", "dsp",     "matrix",   "gpu",     "random", "memmodel", "reflect",
};

}

const char* extensionName(Extension::Value e) {
    if (static_cast<unsigned>(e) >= static_cast<unsigned>(Extension::ExtensionCount)) return "<invalid>";
    return kExtensionNames[static_cast<unsigned>(e)];
}

bool extensionFromName(const std::string& name, Extension::Value& out) {
    for (unsigned i = 0; i < static_cast<unsigned>(Extension::ExtensionCount); ++i) {
        if (name == kExtensionNames[i]) {
            out = static_cast<Extension::Value>(i);
            return true;
        }
    }
    return false;
}

}
