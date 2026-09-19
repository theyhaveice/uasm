#pragma once

#include <string>

namespace uasm {

namespace Extension {
enum Value {
    Core = 0,
    Simd128,
    Simd256,
    Simd512,
    Atomic,
    Thread,
    Coroutine,
    Exception,
    Float16,
    Decimal,
    Fixed,
    BigInt,
    Crypto,
    Crc,
    Bitfield,
    Dsp,
    Matrix,
    Gpu,
    Random,
    MemModel,
    Reflect,
    ExtensionCount
};
}

const char* extensionName(Extension::Value e);

bool extensionFromName(const std::string& name, Extension::Value& out);

class ExtensionSet {
public:
    ExtensionSet() : bits_(1u) {}

    void enable(Extension::Value e) { bits_ |= mask(e); }
    void disable(Extension::Value e) {
        if (e != Extension::Core) bits_ &= ~mask(e);
    }
    bool has(Extension::Value e) const { return (bits_ & mask(e)) != 0; }
    void enableAll() { bits_ = ~0u; }
    unsigned long bits() const { return bits_; }
    void setBits(unsigned long b) { bits_ = b | 1u; }

private:
    static unsigned long mask(Extension::Value e) { return 1ul << static_cast<unsigned>(e); }
    unsigned long bits_;
};

}
