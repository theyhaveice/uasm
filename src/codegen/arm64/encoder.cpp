#include "../codegen_internal.h"

#include <cstring>
#include <map>

namespace uasm {

namespace {

void emit32(std::vector<uint8_t>& out, uint32_t word) {
    out.push_back(static_cast<uint8_t>(word & 0xFF));
    out.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
}

const int kScratch0 = 9;
const int kScratch1 = 10;
const int kScratch2 = 11;
const int kFrameBaseOffset = 16;

void emitLoadImm64(std::vector<uint8_t>& out, int rd, uint64_t imm) {
    emit32(out, 0xD2800000u | (static_cast<uint32_t>(imm & 0xFFFF) << 5) | static_cast<uint32_t>(rd));
    uint32_t chunk1 = static_cast<uint32_t>((imm >> 16) & 0xFFFF);
    uint32_t chunk2 = static_cast<uint32_t>((imm >> 32) & 0xFFFF);
    uint32_t chunk3 = static_cast<uint32_t>((imm >> 48) & 0xFFFF);
    emit32(out, 0xF2A00000u | (chunk1 << 5) | static_cast<uint32_t>(rd));
    emit32(out, 0xF2C00000u | (chunk2 << 5) | static_cast<uint32_t>(rd));
    emit32(out, 0xF2E00000u | (chunk3 << 5) | static_cast<uint32_t>(rd));
}

void emitStrSlot(std::vector<uint8_t>& out, int rt, uint32_t slot) {
    uint32_t off = kFrameBaseOffset + slot * 8;
    emit32(out, 0xF9000000u | ((off / 8) << 10) | (31u << 5) | static_cast<uint32_t>(rt));
}

void emitLdrSlot(std::vector<uint8_t>& out, int rt, uint32_t slot) {
    uint32_t off = kFrameBaseOffset + slot * 8;
    emit32(out, 0xF9400000u | ((off / 8) << 10) | (31u << 5) | static_cast<uint32_t>(rt));
}

void emitLoadOperand(std::vector<uint8_t>& out, int scratchReg, const Operand& op) {
    if (op.kind == Operand::Register) {
        emitLdrSlot(out, scratchReg, op.reg);
    } else if (op.kind == Operand::ImmediateInt) {
        emitLoadImm64(out, scratchReg, static_cast<uint64_t>(op.immInt));
    } else {
        throw CodegenError("arm64 backend does not support float immediates yet");
    }
}

void emitAdd(std::vector<uint8_t>& out, int rd, int rn, int rm) {
    emit32(out, 0x8B000000u | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) | static_cast<uint32_t>(rd));
}

void emitSub(std::vector<uint8_t>& out, int rd, int rn, int rm) {
    emit32(out, 0xCB000000u | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) | static_cast<uint32_t>(rd));
}

void emitMul(std::vector<uint8_t>& out, int rd, int rn, int rm) {
    emit32(out, 0x9B000000u | (static_cast<uint32_t>(rm) << 16) | (31u << 10) | (static_cast<uint32_t>(rn) << 5) |
                     static_cast<uint32_t>(rd));
}

void emitSdiv(std::vector<uint8_t>& out, int rd, int rn, int rm) {
    emit32(out, 0x9AC00C00u | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) | static_cast<uint32_t>(rd));
}

void emitCmp(std::vector<uint8_t>& out, int rn, int rm) {
    emit32(out, 0xEB00001Fu | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5));
}

size_t emitBCond(std::vector<uint8_t>& out, uint32_t cond) {
    size_t at = out.size();
    emit32(out, 0x54000000u | cond);
    return at;
}

size_t emitB(std::vector<uint8_t>& out) {
    size_t at = out.size();
    emit32(out, 0x14000000u);
    return at;
}

size_t emitBL(std::vector<uint8_t>& out) {
    size_t at = out.size();
    emit32(out, 0x94000000u);
    return at;
}

void patchBranch26(std::vector<uint8_t>& code, size_t at, int64_t deltaBytes) {
    uint32_t word;
    std::memcpy(&word, &code[at], 4);
    int32_t imm26 = static_cast<int32_t>(deltaBytes / 4);
    word = (word & 0xFC000000u) | (static_cast<uint32_t>(imm26) & 0x3FFFFFFu);
    std::memcpy(&code[at], &word, 4);
}

void patchBCond19(std::vector<uint8_t>& code, size_t at, int64_t deltaBytes) {
    uint32_t word;
    std::memcpy(&word, &code[at], 4);
    int32_t imm19 = static_cast<int32_t>(deltaBytes / 4);
    word = (word & 0xFF00001Fu) | ((static_cast<uint32_t>(imm19) & 0x7FFFFu) << 5);
    std::memcpy(&code[at], &word, 4);
}

void emitRet(std::vector<uint8_t>& out) { emit32(out, 0xD65F03C0u); }

void emitPrologue(std::vector<uint8_t>& out, uint32_t frameSize) {
    emit32(out, 0xA9BF7BFDu);
    if (frameSize > 0) {
        emit32(out, 0xD10003FFu | (frameSize << 10));
    }
}

void emitEpilogue(std::vector<uint8_t>& out, uint32_t frameSize) {
    if (frameSize > 0) {
        emit32(out, 0x910003FFu | (frameSize << 10));
    }
    emit32(out, 0xA8C17BFDu);
}

uint32_t condCode(Opcode::Value op) {
    switch (op) {
        case Opcode::Beq: return 0x0;
        case Opcode::Bne: return 0x1;
        case Opcode::Bge: return 0xA;
        case Opcode::Blt: return 0xB;
        case Opcode::Bgt: return 0xC;
        case Opcode::Ble: return 0xD;
        default: throw CodegenError("not a branch opcode");
    }
}

bool isSupportedOpcode(Opcode::Value op) {
    switch (op) {
        case Opcode::Mov:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Cmp:
        case Opcode::Beq:
        case Opcode::Bne:
        case Opcode::Blt:
        case Opcode::Bgt:
        case Opcode::Ble:
        case Opcode::Bge:
        case Opcode::Jmp:
        case Opcode::Call:
        case Opcode::Ret:
        case Opcode::RetVoid:
            return true;
        default:
            return false;
    }
}

bool isFloatType(Type::Value t) { return t == Type::F32 || t == Type::F64; }

struct PendingBranch {
    size_t at;
    std::string label;
    bool isConditional;
};

}

bool arm64IsSupported(const Function& fn) {
    for (size_t b = 0; b < fn.blocks.size(); ++b) {
        const Block& block = fn.blocks[b];
        for (size_t i = 0; i < block.instructions.size(); ++i) {
            const Instruction& instr = block.instructions[i];
            if (!isSupportedOpcode(instr.opcode)) return false;
            if (isFloatType(instr.type)) return false;
        }
    }
    return true;
}

CompiledFunction arm64CompileFunction(const Function& fn) {
    if (!arm64IsSupported(fn)) {
        throw CodegenError("function '" + fn.name +
                            "' uses instructions the arm64 backend doesn't support yet "
                            "(only integer mov/add/sub/mul/div/cmp/branches/call/ret in v0.4)");
    }

    CompiledFunction out;
    out.name = fn.name;

    uint32_t maxReg = static_cast<uint32_t>(fn.params.size());
    for (size_t b = 0; b < fn.blocks.size(); ++b) {
        const Block& block = fn.blocks[b];
        for (size_t i = 0; i < block.instructions.size(); ++i) {
            const Instruction& instr = block.instructions[i];
            if (instr.hasDest && instr.dest + 1 > maxReg) maxReg = instr.dest + 1;
            for (size_t o = 0; o < instr.operands.size(); ++o) {
                if (instr.operands[o].kind == Operand::Register && instr.operands[o].reg + 1 > maxReg) {
                    maxReg = instr.operands[o].reg + 1;
                }
            }
        }
    }
    uint32_t frameSize = ((maxReg * 8) + 15) & ~15u;

    std::vector<uint8_t>& code = out.code;
    emitPrologue(code, frameSize);

    for (size_t p = 0; p < fn.params.size() && p < 8; ++p) {
        emitStrSlot(code, static_cast<int>(p), static_cast<uint32_t>(p));
    }

    typedef std::map<std::string, size_t> BlockOffsetMap;
    BlockOffsetMap blockOffsets;
    std::vector<PendingBranch> pendingBranches;

    for (size_t b = 0; b < fn.blocks.size(); ++b) {
        const Block& block = fn.blocks[b];
        blockOffsets[block.label] = code.size();

        for (size_t i = 0; i < block.instructions.size(); ++i) {
            const Instruction& instr = block.instructions[i];
            switch (instr.opcode) {
                case Opcode::Mov: {
                    emitLoadOperand(code, kScratch0, instr.operands[0]);
                    emitStrSlot(code, kScratch0, instr.dest);
                    break;
                }
                case Opcode::Add:
                case Opcode::Sub:
                case Opcode::Mul:
                case Opcode::Div: {
                    emitLoadOperand(code, kScratch0, instr.operands[0]);
                    emitLoadOperand(code, kScratch1, instr.operands[1]);
                    if (instr.opcode == Opcode::Add) emitAdd(code, kScratch2, kScratch0, kScratch1);
                    else if (instr.opcode == Opcode::Sub) emitSub(code, kScratch2, kScratch0, kScratch1);
                    else if (instr.opcode == Opcode::Mul) emitMul(code, kScratch2, kScratch0, kScratch1);
                    else emitSdiv(code, kScratch2, kScratch0, kScratch1);
                    emitStrSlot(code, kScratch2, instr.dest);
                    break;
                }
                case Opcode::Cmp: {
                    emitLoadOperand(code, kScratch0, instr.operands[0]);
                    emitLoadOperand(code, kScratch1, instr.operands[1]);
                    emitCmp(code, kScratch0, kScratch1);
                    break;
                }
                case Opcode::Jmp: {
                    PendingBranch pb;
                    pb.at = emitB(code);
                    pb.label = instr.operands[0].name;
                    pb.isConditional = false;
                    pendingBranches.push_back(pb);
                    break;
                }
                case Opcode::Beq:
                case Opcode::Bne:
                case Opcode::Blt:
                case Opcode::Bgt:
                case Opcode::Ble:
                case Opcode::Bge: {
                    PendingBranch pb;
                    pb.at = emitBCond(code, condCode(instr.opcode));
                    pb.label = instr.operands[0].name;
                    pb.isConditional = true;
                    pendingBranches.push_back(pb);
                    break;
                }
                case Opcode::Call: {
                    for (size_t a = 1; a < instr.operands.size() && a <= 8; ++a) {
                        emitLoadOperand(code, static_cast<int>(a - 1), instr.operands[a]);
                    }
                    CallFixup fixup;
                    fixup.byteOffset = emitBL(code);
                    fixup.calleeName = instr.operands[0].name;
                    out.callFixups.push_back(fixup);
                    if (instr.hasDest) emitStrSlot(code, 0, instr.dest);
                    break;
                }
                case Opcode::Ret: {
                    emitLoadOperand(code, 0, instr.operands[0]);
                    emitEpilogue(code, frameSize);
                    emitRet(code);
                    break;
                }
                case Opcode::RetVoid: {
                    emitEpilogue(code, frameSize);
                    emitRet(code);
                    break;
                }
                default:
                    throw CodegenError("unreachable: unsupported opcode reached arm64 emission");
            }
        }
    }

    for (size_t i = 0; i < pendingBranches.size(); ++i) {
        BlockOffsetMap::const_iterator it = blockOffsets.find(pendingBranches[i].label);
        if (it == blockOffsets.end()) {
            throw CodegenError("branch to unknown block '" + pendingBranches[i].label + "' in function '" + fn.name + "'");
        }
        int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(pendingBranches[i].at);
        if (pendingBranches[i].isConditional) patchBCond19(code, pendingBranches[i].at, delta);
        else patchBranch26(code, pendingBranches[i].at, delta);
    }

    return out;
}

}
