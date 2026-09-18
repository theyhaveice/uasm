#include "../codegen_internal.h"

#include <cstring>

namespace uasm {

namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    appendU32(out, static_cast<uint32_t>(v & 0xFFFFFFFFu));
    appendU32(out, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

void appendName16(std::vector<uint8_t>& out, const char* name) {
    size_t len = std::strlen(name);
    for (size_t i = 0; i < 16; ++i) out.push_back(i < len ? static_cast<uint8_t>(name[i]) : 0);
}

const uint64_t kBaseAddr = 0x100000000;
const uint32_t kMhMagic64 = 0xFEEDFACFu;
const uint32_t kCpuTypeArm64 = 0x0100000Cu;
const uint32_t kCpuSubtypeArm64All = 0x00000000u;
const uint32_t kMhExecute = 0x2u;
const uint32_t kLcSegment64 = 0x19u;
const uint32_t kLcUnixthread = 0x5u;
const uint32_t kArmThreadState64 = 6u;
const uint32_t kThreadStateWords = 68u;

}

std::vector<uint8_t> machoWrapExecutable(TargetArch::Value arch, const CompiledCode& code) {
    if (arch != TargetArch::Arm64) throw CodegenError("macOS executable writer only supports arm64 in v0.4");

    const uint32_t segment64Size = 72;
    const uint32_t section64Size = 80;
    const uint32_t threadHeaderSize = 16;
    const uint32_t threadStateSize = kThreadStateWords * 4;

    const uint32_t cmdPageZeroSize = segment64Size;
    const uint32_t cmdTextSize = segment64Size + section64Size;
    const uint32_t cmdThreadSize = threadHeaderSize + threadStateSize;
    const uint32_t ncmds = 3;
    const uint32_t sizeofcmds = cmdPageZeroSize + cmdTextSize + cmdThreadSize;
    const uint32_t headerSize = 32;

    const uint64_t textFileStart = headerSize + sizeofcmds;
    const uint64_t textVmAddr = kBaseAddr;
    const uint64_t codeAddr = textVmAddr + textFileStart;
    const uint64_t fileSize = textFileStart + code.code.size();
    const uint64_t pageSize = 0x1000;
    const uint64_t textVmSize = ((fileSize + pageSize - 1) / pageSize) * pageSize;

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(fileSize));

    appendU32(out, kMhMagic64);
    appendU32(out, kCpuTypeArm64);
    appendU32(out, kCpuSubtypeArm64All);
    appendU32(out, kMhExecute);
    appendU32(out, ncmds);
    appendU32(out, sizeofcmds);
    appendU32(out, 0x1u);
    appendU32(out, 0);

    appendU32(out, kLcSegment64);
    appendU32(out, cmdPageZeroSize);
    appendName16(out, "__PAGEZERO");
    appendU64(out, 0);
    appendU64(out, kBaseAddr);
    appendU64(out, 0);
    appendU64(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);

    appendU32(out, kLcSegment64);
    appendU32(out, cmdTextSize);
    appendName16(out, "__TEXT");
    appendU64(out, textVmAddr);
    appendU64(out, textVmSize);
    appendU64(out, 0);
    appendU64(out, fileSize);
    appendU32(out, 0x5u);
    appendU32(out, 0x5u);
    appendU32(out, 1);
    appendU32(out, 0);

    appendName16(out, "__text");
    appendName16(out, "__TEXT");
    appendU64(out, codeAddr);
    appendU64(out, code.code.size());
    appendU32(out, static_cast<uint32_t>(textFileStart));
    appendU32(out, 2);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0x80000400u);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);

    appendU32(out, kLcUnixthread);
    appendU32(out, cmdThreadSize);
    appendU32(out, kArmThreadState64);
    appendU32(out, kThreadStateWords);
    for (int i = 0; i < 29; ++i) appendU64(out, 0);
    appendU64(out, 0);
    appendU64(out, 0);
    appendU64(out, 0);
    appendU64(out, codeAddr + code.entryOffset);
    appendU32(out, 0);
    appendU32(out, 0);

    out.insert(out.end(), code.code.begin(), code.code.end());

    return out;
}

}
