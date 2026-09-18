#include "uasm/serializer.h"

#include "uasm/compat.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace uasm {

namespace {

class Writer {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void i64(int64_t v) {
        uint64_t u = static_cast<uint64_t>(v);
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
    }
    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

void writeOperand(Writer& w, const Operand& op) {
    switch (op.kind) {
        case Operand::Register:
            w.u8(0);
            w.u32(op.reg);
            break;
        case Operand::ImmediateInt:
            w.u8(1);
            w.i64(op.immInt);
            break;
        case Operand::ImmediateFloat:
            w.u8(2);
            w.f64(op.immFloat);
            break;
        case Operand::Symbol:
        case Operand::Label:
            w.u8(3);
            w.str(op.name);
            break;
    }
}

void writeInstruction(Writer& w, const Instruction& instr) {
    w.u8(static_cast<uint8_t>(instr.opcode));
    w.u8(static_cast<uint8_t>(instr.type));
    w.u8(instr.hasDest ? 1 : 0);
    w.u32(instr.dest);
    w.u8(static_cast<uint8_t>(instr.operands.size()));
    for (size_t i = 0; i < instr.operands.size(); ++i) writeOperand(w, instr.operands[i]);
}

void writeFunction(Writer& w, const Function& fn) {
    w.str(fn.name);
    w.str(fn.module);
    w.u8(fn.isExported ? 1 : 0);
    w.u8(static_cast<uint8_t>(fn.params.size()));
    for (size_t i = 0; i < fn.params.size(); ++i) w.u8(static_cast<uint8_t>(fn.params[i].type));
    w.u8(static_cast<uint8_t>(fn.returnType));
    w.u32(static_cast<uint32_t>(fn.blocks.size()));
    for (size_t i = 0; i < fn.blocks.size(); ++i) {
        const Block& block = fn.blocks[i];
        w.str(block.label);
        w.u32(static_cast<uint32_t>(block.instructions.size()));
        for (size_t j = 0; j < block.instructions.size(); ++j) writeInstruction(w, block.instructions[j]);
    }
}

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

    uint8_t u8() {
        need(1);
        return data_[pos_++];
    }
    uint32_t u32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data_[pos_ + i]) << (i * 8);
        pos_ += 4;
        return v;
    }
    int64_t i64() {
        need(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
        pos_ += 8;
        return static_cast<int64_t>(v);
    }
    double f64() {
        uint64_t bits = static_cast<uint64_t>(i64());
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    std::string str() {
        uint32_t len = u32();
        need(len);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }
    bool atEnd() const { return pos_ >= size_; }

private:
    void need(size_t n) const {
        if (pos_ + n > size_) throw SerializeError("unexpected end of .uo file");
    }
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

Operand readOperand(Reader& r) {
    Operand op;
    uint8_t kind = r.u8();
    switch (kind) {
        case 0:
            op.kind = Operand::Register;
            op.reg = r.u32();
            break;
        case 1:
            op.kind = Operand::ImmediateInt;
            op.immInt = r.i64();
            break;
        case 2:
            op.kind = Operand::ImmediateFloat;
            op.immFloat = r.f64();
            break;
        case 3:
            op.kind = Operand::Symbol;
            op.name = r.str();
            break;
        default:
            throw SerializeError("corrupt operand kind in .uo file");
    }
    return op;
}

Instruction readInstruction(Reader& r) {
    Instruction instr;
    instr.opcode = static_cast<Opcode::Value>(r.u8());
    instr.type = static_cast<Type::Value>(r.u8());
    instr.hasDest = r.u8() != 0;
    instr.dest = r.u32();
    uint8_t count = r.u8();
    for (uint8_t i = 0; i < count; ++i) instr.operands.push_back(readOperand(r));
    return instr;
}

Function readFunction(Reader& r) {
    Function fn;
    fn.name = r.str();
    fn.module = r.str();
    fn.isExported = r.u8() != 0;
    uint8_t paramCount = r.u8();
    for (uint8_t i = 0; i < paramCount; ++i) {
        Param p;
        p.type = static_cast<Type::Value>(r.u8());
        fn.params.push_back(p);
    }
    fn.returnType = static_cast<Type::Value>(r.u8());
    uint32_t blockCount = r.u32();
    for (uint32_t i = 0; i < blockCount; ++i) {
        Block block;
        block.label = r.str();
        uint32_t instrCount = r.u32();
        for (uint32_t j = 0; j < instrCount; ++j) block.instructions.push_back(readInstruction(r));
        fn.blocks.push_back(UASM_MOVE(block));
    }
    return fn;
}

}

void writeUo(const Program& program, const std::string& path) {
    Writer w;
    w.u32(static_cast<uint32_t>(program.functions.size()));
    for (size_t i = 0; i < program.functions.size(); ++i) writeFunction(w, program.functions[i]);
    w.u32(program.entryIndex);

    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) throw SerializeError("could not open '" + path + "' for writing");
    out.write(kMagic, sizeof(kMagic));
    out.write(reinterpret_cast<const char*>(&w.data()[0]), static_cast<std::streamsize>(w.data().size()));
    if (!out) throw SerializeError("failed writing '" + path + "'");
}

Program readUo(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) throw SerializeError("could not open '" + path + "' for reading");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (bytes.size() < sizeof(kMagic) || std::memcmp(&bytes[0], kMagic, sizeof(kMagic)) != 0) {
        throw SerializeError("'" + path + "' is not a valid .uo file (bad magic)");
    }

    Reader r(&bytes[0] + sizeof(kMagic), bytes.size() - sizeof(kMagic));
    Program program;
    uint32_t functionCount = r.u32();
    for (uint32_t i = 0; i < functionCount; ++i) program.functions.push_back(readFunction(r));
    program.entryIndex = r.u32();
    return program;
}

}
