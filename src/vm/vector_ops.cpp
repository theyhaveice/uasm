#include "vector_ops.h"

#include "uasm/interpreter.h"
#include "uasm/opcode_info.h"

#include <cmath>
#include <cstring>

namespace uasm {

namespace {

bool isFloatLane(Type::Value t) { return t == Type::F32 || t == Type::F64; }

bool isSignedLane(Type::Value t) {
    return t == Type::I8 || t == Type::I16 || t == Type::I32 || t == Type::I64 || t == Type::I128;
}

size_t laneBits(Type::Value t) { return sizeOfType(t) * 8; }

UInt128 laneMask(size_t bits) {
    if (bits >= 128) return ~static_cast<UInt128>(0);
    return (static_cast<UInt128>(1) << bits) - 1;
}

UInt128 laneUnsigned(const Value& v, Type::Value lane) {
    return static_cast<UInt128>(v.asInt128()) & laneMask(laneBits(lane));
}

Int128 laneSignedMin(size_t bits) {
    if (bits >= 128) return static_cast<Int128>(static_cast<UInt128>(1) << 127);
    return -(static_cast<Int128>(1) << (bits - 1));
}

Int128 laneSignedMax(size_t bits) {
    if (bits >= 128) return static_cast<Int128>((~static_cast<UInt128>(0)) >> 1);
    return (static_cast<Int128>(1) << (bits - 1)) - 1;
}

void requireVector(Type::Value width) {
    if (!isVectorType(width)) throw RuntimeError("this instruction requires a vector width (v128/v256/v512)");
}

void requireLane(Type::Value lane) {
    if (isVectorType(lane) || lane == Type::Void) throw RuntimeError("invalid vector lane type");
}

Value allOnes(Type::Value lane) { return Value::fromInt128(lane, static_cast<Int128>(-1)); }

Value allZero(Type::Value lane) { return Value::fromInt128(lane, 0); }

double laneDouble(const Value& v) { return v.asDouble(); }

Value laneFromDouble(Type::Value lane, double d) { return Value::fromDouble(lane, d); }

Value laneFromInt(Type::Value lane, Int128 v) { return Value::fromInt128(lane, v); }

bool laneCompare(Opcode::Value op, Type::Value lane, const Value& a, const Value& b) {
    if (op == Opcode::VCmpord || op == Opcode::VCmpuno) {
        double x = laneDouble(a);
        double y = laneDouble(b);
        bool unordered = (x != x) || (y != y);
        return op == Opcode::VCmpuno ? unordered : !unordered;
    }

    int flag;
    bool unsignedCmp = op == Opcode::VUcmplt || op == Opcode::VUcmpgt || op == Opcode::VUcmple || op == Opcode::VUcmpge;
    if (unsignedCmp) {
        UInt128 x = laneUnsigned(a, lane);
        UInt128 y = laneUnsigned(b, lane);
        flag = x < y ? -1 : (x > y ? 1 : 0);
    } else if (isFloatLane(lane)) {
        double x = laneDouble(a);
        double y = laneDouble(b);
        flag = x < y ? -1 : (x > y ? 1 : 0);
        if (x != x || y != y) return op == Opcode::VCmpne;
    } else {
        Int128 x = a.asInt128();
        Int128 y = b.asInt128();
        flag = x < y ? -1 : (x > y ? 1 : 0);
    }

    switch (op) {
        case Opcode::VCmpeq: return flag == 0;
        case Opcode::VCmpne: return flag != 0;
        case Opcode::VCmplt:
        case Opcode::VUcmplt: return flag < 0;
        case Opcode::VCmpgt:
        case Opcode::VUcmpgt: return flag > 0;
        case Opcode::VCmple:
        case Opcode::VUcmple: return flag <= 0;
        case Opcode::VCmpge:
        case Opcode::VUcmpge: return flag >= 0;
        default: throw RuntimeError("not a vector comparison opcode");
    }
}

Value laneBinary(Opcode::Value op, Type::Value lane, const Value& a, const Value& b) {
    size_t bits = laneBits(lane);
    if (isFloatLane(lane)) {
        double x = laneDouble(a);
        double y = laneDouble(b);
        switch (op) {
            case Opcode::VAdd: return laneFromDouble(lane, x + y);
            case Opcode::VSub: return laneFromDouble(lane, x - y);
            case Opcode::VMul: return laneFromDouble(lane, x * y);
            case Opcode::VDiv: return laneFromDouble(lane, x / y);
            case Opcode::VMin: return laneFromDouble(lane, x < y ? x : y);
            case Opcode::VMax: return laneFromDouble(lane, x > y ? x : y);
            case Opcode::VFmin: return laneFromDouble(lane, (x != x) ? y : ((y != y) ? x : (x < y ? x : y)));
            case Opcode::VFmax: return laneFromDouble(lane, (x != x) ? y : ((y != y) ? x : (x > y ? x : y)));
            case Opcode::VAvg: return laneFromDouble(lane, (x + y) / 2.0);
            case Opcode::VAbsdiff: return laneFromDouble(lane, x > y ? x - y : y - x);
            case Opcode::VCopysign: return laneFromDouble(lane, (y < 0.0) ? -std::fabs(x) : std::fabs(x));
            case Opcode::VHadd: return laneFromDouble(lane, x + y);
            case Opcode::VHsub: return laneFromDouble(lane, x - y);
            default: break;
        }
    }

    Int128 x = a.asInt128();
    Int128 y = b.asInt128();
    UInt128 ux = laneUnsigned(a, lane);
    UInt128 uy = laneUnsigned(b, lane);

    switch (op) {
        case Opcode::VAdd: return laneFromInt(lane, x + y);
        case Opcode::VSub: return laneFromInt(lane, x - y);
        case Opcode::VMul: return laneFromInt(lane, x * y);
        case Opcode::VDiv:
            if (y == 0) throw RuntimeError("vector division by zero");
            return laneFromInt(lane, x / y);
        case Opcode::VMin: return laneFromInt(lane, x < y ? x : y);
        case Opcode::VMax: return laneFromInt(lane, x > y ? x : y);
        case Opcode::VFmin: return laneFromInt(lane, x < y ? x : y);
        case Opcode::VFmax: return laneFromInt(lane, x > y ? x : y);
        case Opcode::VAnd: return laneFromInt(lane, x & y);
        case Opcode::VOr: return laneFromInt(lane, x | y);
        case Opcode::VXor: return laneFromInt(lane, x ^ y);
        case Opcode::VAndnot: return laneFromInt(lane, x & ~y);
        case Opcode::VAvg: return laneFromInt(lane, static_cast<Int128>((ux & uy) + ((ux ^ uy) >> 1)));
        case Opcode::VAbsdiff: return laneFromInt(lane, x > y ? x - y : y - x);
        case Opcode::VHadd: return laneFromInt(lane, x + y);
        case Opcode::VHsub: return laneFromInt(lane, x - y);
        case Opcode::VShl:
        case Opcode::VShr:
        case Opcode::VSar:
        case Opcode::VRotl:
        case Opcode::VRotr: {
            if (y < 0 || static_cast<size_t>(y) >= bits) throw RuntimeError("vector shift amount out of range");
            size_t s = static_cast<size_t>(y);
            if (op == Opcode::VShl) return laneFromInt(lane, static_cast<Int128>((ux << s) & laneMask(bits)));
            if (op == Opcode::VShr) return laneFromInt(lane, static_cast<Int128>(ux >> s));
            if (op == Opcode::VSar) return laneFromInt(lane, x >> static_cast<int>(s));
            if (s == 0) return laneFromInt(lane, static_cast<Int128>(ux));
            if (op == Opcode::VRotl) {
                return laneFromInt(lane, static_cast<Int128>(((ux << s) | (ux >> (bits - s))) & laneMask(bits)));
            }
            return laneFromInt(lane, static_cast<Int128>(((ux >> s) | (ux << (bits - s))) & laneMask(bits)));
        }
        case Opcode::VAddsat: {
            Int128 r = x + y;
            Int128 lo = laneSignedMin(bits);
            Int128 hi = laneSignedMax(bits);
            if (!isSignedLane(lane)) {
                UInt128 ur = ux + uy;
                if (ur > laneMask(bits)) ur = laneMask(bits);
                return laneFromInt(lane, static_cast<Int128>(ur));
            }
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return laneFromInt(lane, r);
        }
        case Opcode::VSubsat: {
            if (!isSignedLane(lane)) return laneFromInt(lane, static_cast<Int128>(ux < uy ? 0 : ux - uy));
            Int128 r = x - y;
            Int128 lo = laneSignedMin(bits);
            Int128 hi = laneSignedMax(bits);
            if (r > hi) r = hi;
            if (r < lo) r = lo;
            return laneFromInt(lane, r);
        }
        case Opcode::VMulhi: {
            if (bits > 64) throw RuntimeError("vmulhi is not defined for 128-bit lanes");
            if (isSignedLane(lane)) return laneFromInt(lane, (x * y) >> static_cast<int>(bits));
            return laneFromInt(lane, static_cast<Int128>((ux * uy) >> static_cast<int>(bits)));
        }
        default: throw RuntimeError(std::string("not a binary vector opcode: ") + opcodeName(op));
    }
}

Value laneUnary(Opcode::Value op, Type::Value lane, const Value& a) {
    size_t bits = laneBits(lane);
    if (isFloatLane(lane)) {
        double x = laneDouble(a);
        switch (op) {
            case Opcode::VAbs: return laneFromDouble(lane, std::fabs(x));
            case Opcode::VNeg: return laneFromDouble(lane, -x);
            case Opcode::VSqrt: return laneFromDouble(lane, std::sqrt(x));
            case Opcode::VRecip: return laneFromDouble(lane, 1.0 / x);
            case Opcode::VRsqrt: return laneFromDouble(lane, 1.0 / std::sqrt(x));
            case Opcode::VFloor: return laneFromDouble(lane, std::floor(x));
            case Opcode::VCeil: return laneFromDouble(lane, std::ceil(x));
            case Opcode::VRound: return laneFromDouble(lane, std::floor(x + 0.5));
            case Opcode::VTrunc: return laneFromDouble(lane, std::trunc(x));
            case Opcode::VNearest: {
                double r = std::floor(x + 0.5);
                if (r - x == 0.5 && std::fmod(r, 2.0) != 0.0) r -= 1.0;
                return laneFromDouble(lane, r);
            }
            case Opcode::VNot: break;
            default: break;
        }
    }

    Int128 x = a.asInt128();
    UInt128 ux = laneUnsigned(a, lane);
    switch (op) {
        case Opcode::VAbs: return laneFromInt(lane, x < 0 ? -x : x);
        case Opcode::VNeg: return laneFromInt(lane, -x);
        case Opcode::VNot: return laneFromInt(lane, ~x);
        case Opcode::VSqrt: {
            if (x < 0) throw RuntimeError("vsqrt of a negative integer lane");
            UInt128 n = static_cast<UInt128>(x);
            if (n == 0) return laneFromInt(lane, 0);
            UInt128 g = n, h = (g + 1) / 2;
            while (h < g) { g = h; h = (g + n / g) / 2; }
            return laneFromInt(lane, static_cast<Int128>(g));
        }
        case Opcode::VPopcount: {
            int c = 0;
            for (size_t i = 0; i < bits; ++i) {
                if ((ux >> i) & 1) ++c;
            }
            return laneFromInt(lane, c);
        }
        case Opcode::VClz: {
            int c = static_cast<int>(bits);
            for (size_t i = bits; i-- > 0;) {
                if ((ux >> i) & 1) { c = static_cast<int>(bits - 1 - i); break; }
            }
            return laneFromInt(lane, c);
        }
        case Opcode::VCtz: {
            int c = static_cast<int>(bits);
            for (size_t i = 0; i < bits; ++i) {
                if ((ux >> i) & 1) { c = static_cast<int>(i); break; }
            }
            return laneFromInt(lane, c);
        }
        case Opcode::VBswap: {
            UInt128 r = 0;
            size_t n = sizeOfType(lane);
            for (size_t i = 0; i < n; ++i) r = (r << 8) | ((ux >> (i * 8)) & 0xFF);
            return laneFromInt(lane, static_cast<Int128>(r));
        }
        case Opcode::VFloor:
        case Opcode::VCeil:
        case Opcode::VRound:
        case Opcode::VTrunc:
        case Opcode::VNearest:
            return laneFromInt(lane, x);
        default: throw RuntimeError(std::string("not a unary vector opcode: ") + opcodeName(op));
    }
}

bool laneTruthy(const Value& v, Type::Value lane) { return laneUnsigned(v, lane) != 0; }

bool laneSignBit(const Value& v, Type::Value lane) {
    return ((laneUnsigned(v, lane) >> (laneBits(lane) - 1)) & 1) != 0;
}

}

std::size_t vectorLaneCount(Type::Value width, Type::Value lane) {
    requireVector(width);
    requireLane(lane);
    size_t ls = sizeOfType(lane);
    if (ls == 0) throw RuntimeError("invalid vector lane type");
    return vectorBytes(width) / ls;
}

Value vectorGetLane(const Value& v, Type::Value lane, std::size_t index) {
    Value r;
    r.type = lane;
    std::memcpy(&r.bits, &v.bits.vec[index * sizeOfType(lane)], sizeOfType(lane));
    return r;
}

void vectorSetLane(Value& v, Type::Value lane, std::size_t index, const Value& laneValue) {
    std::memcpy(&v.bits.vec[index * sizeOfType(lane)], &laneValue.bits, sizeOfType(lane));
}

bool vectorOpIsWide(Opcode::Value op) {
    Extension::Value e = opcodeExtension(op);
    return e == Extension::Simd256 || e == Extension::Simd512;
}

int vectorMaskedBase(Opcode::Value op, Opcode::Value& baseOp) {
    switch (op) {
        case Opcode::VAddz: baseOp = Opcode::VAdd; return 1;
        case Opcode::VAddk: baseOp = Opcode::VAdd; return 0;
        case Opcode::VSubz: baseOp = Opcode::VSub; return 1;
        case Opcode::VSubk: baseOp = Opcode::VSub; return 0;
        case Opcode::VMulz: baseOp = Opcode::VMul; return 1;
        case Opcode::VMulk: baseOp = Opcode::VMul; return 0;
        case Opcode::VDivz: baseOp = Opcode::VDiv; return 1;
        case Opcode::VDivk: baseOp = Opcode::VDiv; return 0;
        case Opcode::VMinz: baseOp = Opcode::VMin; return 1;
        case Opcode::VMink: baseOp = Opcode::VMin; return 0;
        case Opcode::VMaxz: baseOp = Opcode::VMax; return 1;
        case Opcode::VMaxk: baseOp = Opcode::VMax; return 0;
        case Opcode::VSqrtz: baseOp = Opcode::VSqrt; return 1;
        case Opcode::VSqrtk: baseOp = Opcode::VSqrt; return 0;
        case Opcode::VAbsz: baseOp = Opcode::VAbs; return 1;
        case Opcode::VAbsk: baseOp = Opcode::VAbs; return 0;
        case Opcode::VNegz: baseOp = Opcode::VNeg; return 1;
        case Opcode::VNegk: baseOp = Opcode::VNeg; return 0;
        case Opcode::VFmaz: baseOp = Opcode::VMla; return 1;
        case Opcode::VFmak: baseOp = Opcode::VMla; return 0;
        case Opcode::VAddb: baseOp = Opcode::VAdd; return 2;
        case Opcode::VSubb: baseOp = Opcode::VSub; return 2;
        case Opcode::VMulb: baseOp = Opcode::VMul; return 2;
        case Opcode::VDivb: baseOp = Opcode::VDiv; return 2;
        case Opcode::VFmab: baseOp = Opcode::VMla; return 2;
        default: return -1;
    }
}

bool vectorOpIsMemory(Opcode::Value op) {
    switch (op) {
        case Opcode::VLoad:
        case Opcode::VLoadu:
        case Opcode::VStore:
        case Opcode::VStoreu:
        case Opcode::VGather:
        case Opcode::VScatter:
        case Opcode::VLoadsplat:
        case Opcode::VMaskload:
        case Opcode::VMaskstore:
        case Opcode::VLoadk:
        case Opcode::VLoadz:
        case Opcode::VStorek:
        case Opcode::VGatherk:
        case Opcode::VScatterk:
        case Opcode::VExpandload:
        case Opcode::VCompressstore:
            return true;
        default:
            return false;
    }
}

namespace {

UInt128 maskBitsOf(const Value& v) { return static_cast<UInt128>(v.asInt128()); }

bool maskBit(const Value& m, size_t i) { return ((maskBitsOf(m) >> i) & 1) != 0; }

Value maskValue(UInt128 bits) { return Value::fromInt128(Type::I64, static_cast<Int128>(bits)); }

Value halfVector(const Value& src, Type::Value lane, size_t laneCount, size_t half) {
    Value out;
    out.type = Type::V128;
    size_t perHalf = laneCount / 2;
    for (size_t i = 0; i < perHalf; ++i) {
        vectorSetLane(out, lane, i, vectorGetLane(src, lane, half * perHalf + i));
    }
    return out;
}

}

Value vectorComputeWide(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args,
                        std::size_t argCount) {
    size_t n = vectorLaneCount(width, lane);
    size_t bits = laneBits(lane);
    Value out;
    out.type = width;

    switch (op) {
        case Opcode::Kset: return maskValue(laneMask(n));
        case Opcode::Kclr: return maskValue(0);
        default: break;
    }

    if (argCount == 0) throw RuntimeError(std::string(opcodeName(op)) + " requires at least one operand");

    switch (op) {
        case Opcode::Kmov: return maskValue(maskBitsOf(args[0]) & laneMask(n));
        case Opcode::Knot: return maskValue(~maskBitsOf(args[0]) & laneMask(n));
        case Opcode::Kand: return maskValue((maskBitsOf(args[0]) & maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kor: return maskValue((maskBitsOf(args[0]) | maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kxor: return maskValue((maskBitsOf(args[0]) ^ maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kandn: return maskValue((~maskBitsOf(args[0]) & maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kxnor: return maskValue(~(maskBitsOf(args[0]) ^ maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kadd: return maskValue((maskBitsOf(args[0]) + maskBitsOf(args[1])) & laneMask(n));
        case Opcode::Kshiftl: {
            Int128 k = args[1].asInt128();
            if (k < 0 || static_cast<size_t>(k) > n) throw RuntimeError("mask shift out of range");
            return maskValue((maskBitsOf(args[0]) << static_cast<size_t>(k)) & laneMask(n));
        }
        case Opcode::Kshiftr: {
            Int128 k = args[1].asInt128();
            if (k < 0 || static_cast<size_t>(k) > n) throw RuntimeError("mask shift out of range");
            return maskValue((maskBitsOf(args[0]) & laneMask(n)) >> static_cast<size_t>(k));
        }
        case Opcode::Ktest:
            return Value::fromInt128(Type::I32, (maskBitsOf(args[0]) & maskBitsOf(args[1]) & laneMask(n)) != 0 ? 1 : 0);
        case Opcode::Kortest:
            return Value::fromInt128(Type::I32, ((maskBitsOf(args[0]) | maskBitsOf(args[1])) & laneMask(n)) != 0 ? 1 : 0);
        case Opcode::Kunpack: {
            UInt128 lo = maskBitsOf(args[0]) & laneMask(n / 2);
            UInt128 hi = maskBitsOf(args[1]) & laneMask(n / 2);
            return maskValue(lo | (hi << (n / 2)));
        }
        case Opcode::Kcvtmask: {
            UInt128 m = 0;
            for (size_t i = 0; i < n; ++i) {
                if (laneUnsigned(vectorGetLane(args[0], lane, i), lane) != 0) m |= static_cast<UInt128>(1) << i;
            }
            return maskValue(m);
        }
        case Opcode::Kcount: {
            UInt128 m = maskBitsOf(args[0]) & laneMask(n);
            int c = 0;
            for (size_t i = 0; i < n; ++i) {
                if ((m >> i) & 1) ++c;
            }
            return Value::fromInt128(Type::I32, c);
        }
        case Opcode::Kfirst:
        case Opcode::Klast: {
            UInt128 m = maskBitsOf(args[0]) & laneMask(n);
            Int128 r = -1;
            for (size_t i = 0; i < n; ++i) {
                if ((m >> i) & 1) {
                    r = static_cast<Int128>(i);
                    if (op == Opcode::Kfirst) break;
                }
            }
            return Value::fromInt128(Type::I32, r);
        }
        case Opcode::Kvalid:
            return Value::fromInt128(Type::I32, (maskBitsOf(args[0]) & ~laneMask(n)) == 0 ? 1 : 0);
        default:
            break;
    }

    switch (op) {
        case Opcode::VLohalf: return halfVector(args[0], lane, n, 0);
        case Opcode::VHihalf: return halfVector(args[0], lane, n, 1);
        case Opcode::VSwaphalves: {
            size_t h = n / 2;
            for (size_t i = 0; i < h; ++i) {
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, h + i));
                vectorSetLane(out, lane, h + i, vectorGetLane(args[0], lane, i));
            }
            return out;
        }
        case Opcode::VCombine: {
            if (argCount < 2) throw RuntimeError("vcombine requires two vectors");
            size_t h = n / 2;
            for (size_t i = 0; i < h; ++i) {
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i));
                vectorSetLane(out, lane, h + i, vectorGetLane(args[1], lane, i));
            }
            return out;
        }
        case Opcode::VExtract128: {
            if (argCount < 2) throw RuntimeError("vextract128 requires an index");
            Int128 k = args[1].asInt128();
            size_t per = 16 / sizeOfType(lane);
            if (k < 0 || static_cast<size_t>(k) * per >= n) throw RuntimeError("128-bit lane index out of range");
            Value r;
            r.type = Type::V128;
            for (size_t i = 0; i < per; ++i) {
                vectorSetLane(r, lane, i, vectorGetLane(args[0], lane, static_cast<size_t>(k) * per + i));
            }
            return r;
        }
        case Opcode::VInsert128: {
            if (argCount < 3) throw RuntimeError("vinsert128 requires an index and a v128");
            Int128 k = args[1].asInt128();
            size_t per = 16 / sizeOfType(lane);
            if (k < 0 || static_cast<size_t>(k) * per >= n) throw RuntimeError("128-bit lane index out of range");
            out = args[0];
            out.type = width;
            for (size_t i = 0; i < per; ++i) {
                vectorSetLane(out, lane, static_cast<size_t>(k) * per + i, vectorGetLane(args[2], lane, i));
            }
            return out;
        }
        case Opcode::VBroadcast128: {
            size_t per = 16 / sizeOfType(lane);
            for (size_t i = 0; i < n; ++i) vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i % per));
            return out;
        }
        case Opcode::VPerm128: {
            if (argCount < 2) throw RuntimeError("vperm128 requires a selector");
            size_t per = 16 / sizeOfType(lane);
            size_t groups = n / per;
            Int128 sel = args[1].asInt128();
            for (size_t g = 0; g < groups; ++g) {
                size_t src = static_cast<size_t>((sel >> (g * 4)) & 0xF);
                if (src >= groups) src = groups - 1;
                for (size_t i = 0; i < per; ++i) {
                    vectorSetLane(out, lane, g * per + i, vectorGetLane(args[0], lane, src * per + i));
                }
            }
            return out;
        }
        case Opcode::VPermvar:
        case Opcode::VPermi64:
        case Opcode::VPermi32:
        case Opcode::VPermb:
        case Opcode::VPermw: {
            if (argCount < 2) throw RuntimeError("permute requires an index vector");
            for (size_t i = 0; i < n; ++i) {
                Int128 k = vectorGetLane(args[1], lane, i).asInt128();
                size_t kk = static_cast<size_t>(k) % n;
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, kk));
            }
            return out;
        }
        case Opcode::VShlv:
        case Opcode::VShrv:
        case Opcode::VSarv:
        case Opcode::VRotlv:
        case Opcode::VRotrv: {
            if (argCount < 2) throw RuntimeError("variable shift requires a shift vector");
            for (size_t i = 0; i < n; ++i) {
                UInt128 x = laneUnsigned(vectorGetLane(args[0], lane, i), lane);
                Int128 sx = vectorGetLane(args[0], lane, i).asInt128();
                Int128 k = vectorGetLane(args[1], lane, i).asInt128();
                if (k < 0 || static_cast<size_t>(k) >= bits) throw RuntimeError("vector shift amount out of range");
                size_t sft = static_cast<size_t>(k);
                UInt128 r;
                if (op == Opcode::VShlv) r = (x << sft) & laneMask(bits);
                else if (op == Opcode::VShrv) r = x >> sft;
                else if (op == Opcode::VSarv) r = static_cast<UInt128>(sx >> static_cast<int>(sft));
                else if (sft == 0) r = x;
                else if (op == Opcode::VRotlv) r = ((x << sft) | (x >> (bits - sft))) & laneMask(bits);
                else r = ((x >> sft) | (x << (bits - sft))) & laneMask(bits);
                vectorSetLane(out, lane, i, laneFromInt(lane, static_cast<Int128>(r)));
            }
            return out;
        }
        case Opcode::VAlign:
        case Opcode::VAlignd:
        case Opcode::VAlignq: {
            if (argCount < 3) throw RuntimeError("valign requires two vectors and a count");
            Int128 k = args[2].asInt128();
            for (size_t i = 0; i < n; ++i) {
                size_t idx = static_cast<size_t>((static_cast<Int128>(i) + k) % static_cast<Int128>(2 * n));
                vectorSetLane(out, lane, i,
                              idx < n ? vectorGetLane(args[0], lane, idx) : vectorGetLane(args[1], lane, idx - n));
            }
            return out;
        }
        case Opcode::VCompress:
        case Opcode::VCompressk: {
            if (argCount < 2) throw RuntimeError("vcompress requires a mask");
            size_t k = 0;
            for (size_t i = 0; i < n; ++i) {
                if (maskBit(args[1], i)) vectorSetLane(out, lane, k++, vectorGetLane(args[0], lane, i));
            }
            return out;
        }
        case Opcode::VExpand:
        case Opcode::VExpandk: {
            if (argCount < 2) throw RuntimeError("vexpand requires a mask");
            size_t k = 0;
            for (size_t i = 0; i < n; ++i) {
                if (maskBit(args[1], i)) vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, k++));
            }
            return out;
        }
        case Opcode::VConflict:
        case Opcode::VConflictd:
        case Opcode::VConflictq: {
            for (size_t i = 0; i < n; ++i) {
                Int128 m = 0;
                Int128 vi = vectorGetLane(args[0], lane, i).asInt128();
                for (size_t j = 0; j < i; ++j) {
                    if (vectorGetLane(args[0], lane, j).asInt128() == vi) m |= static_cast<Int128>(1) << j;
                }
                vectorSetLane(out, lane, i, laneFromInt(lane, m));
            }
            return out;
        }
        case Opcode::VMulwide:
        case Opcode::VMaddwide: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            for (size_t i = 0; i < n; ++i) {
                Int128 a = vectorGetLane(args[0], lane, i).asInt128();
                Int128 b = vectorGetLane(args[1], lane, i).asInt128();
                Int128 r = a * b;
                if (op == Opcode::VMaddwide && argCount >= 3) r += vectorGetLane(args[2], lane, i).asInt128();
                vectorSetLane(out, lane, i, laneFromInt(lane, r));
            }
            return out;
        }
        case Opcode::VDpbusd:
        case Opcode::VDpwssd: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            for (size_t i = 0; i < n; ++i) {
                Int128 acc = (argCount >= 3) ? vectorGetLane(args[2], lane, i).asInt128() : 0;
                acc += vectorGetLane(args[0], lane, i).asInt128() * vectorGetLane(args[1], lane, i).asInt128();
                vectorSetLane(out, lane, i, laneFromInt(lane, acc));
            }
            return out;
        }
        case Opcode::VSad:
        case Opcode::VMpsad: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            Int128 acc = 0;
            for (size_t i = 0; i < n; ++i) {
                Int128 a = vectorGetLane(args[0], lane, i).asInt128();
                Int128 b = vectorGetLane(args[1], lane, i).asInt128();
                acc += a > b ? a - b : b - a;
            }
            vectorSetLane(out, lane, 0, laneFromInt(lane, acc));
            return out;
        }
        case Opcode::VReduce2:
        case Opcode::VReduce4: {
            size_t group = (op == Opcode::VReduce2) ? 2 : 4;
            for (size_t i = 0; i + group <= n; i += group) {
                if (isFloatLane(lane)) {
                    double acc = 0;
                    for (size_t j = 0; j < group; ++j) acc += vectorGetLane(args[0], lane, i + j).asDouble();
                    vectorSetLane(out, lane, i / group, laneFromDouble(lane, acc));
                } else {
                    Int128 acc = 0;
                    for (size_t j = 0; j < group; ++j) acc += vectorGetLane(args[0], lane, i + j).asInt128();
                    vectorSetLane(out, lane, i / group, laneFromInt(lane, acc));
                }
            }
            return out;
        }
        case Opcode::VTernlog:
        case Opcode::VTernlogq:
        case Opcode::VTernlogd: {
            if (argCount < 4) throw RuntimeError("vternlog requires three vectors and an immediate");
            UInt128 imm = static_cast<UInt128>(args[3].asInt128()) & 0xFF;
            for (size_t i = 0; i < n; ++i) {
                UInt128 a = laneUnsigned(vectorGetLane(args[0], lane, i), lane);
                UInt128 b = laneUnsigned(vectorGetLane(args[1], lane, i), lane);
                UInt128 c = laneUnsigned(vectorGetLane(args[2], lane, i), lane);
                UInt128 r = 0;
                for (size_t bit = 0; bit < bits; ++bit) {
                    unsigned idx = static_cast<unsigned>((((a >> bit) & 1) << 2) | (((b >> bit) & 1) << 1) |
                                                          ((c >> bit) & 1));
                    if ((imm >> idx) & 1) r |= static_cast<UInt128>(1) << bit;
                }
                vectorSetLane(out, lane, i, laneFromInt(lane, static_cast<Int128>(r)));
            }
            return out;
        }
        case Opcode::VSignselect:
        case Opcode::VBlendlane: {
            if (argCount < 3) throw RuntimeError("this instruction requires three vectors");
            for (size_t i = 0; i < n; ++i) {
                bool take = laneSignBit(vectorGetLane(args[2], lane, i), lane);
                vectorSetLane(out, lane, i, vectorGetLane(args[take ? 1 : 0], lane, i));
            }
            return out;
        }
        case Opcode::VLzcntv:
        case Opcode::VLzcntd:
        case Opcode::VLzcntq:
            for (size_t i = 0; i < n; ++i) {
                vectorSetLane(out, lane, i, laneUnary(Opcode::VClz, lane, vectorGetLane(args[0], lane, i)));
            }
            return out;
        case Opcode::VPopcntv:
        case Opcode::VPopcntd:
        case Opcode::VPopcntq:
        case Opcode::VPopcntb:
        case Opcode::VPopcntw:
            for (size_t i = 0; i < n; ++i) {
                vectorSetLane(out, lane, i, laneUnary(Opcode::VPopcount, lane, vectorGetLane(args[0], lane, i)));
            }
            return out;
        case Opcode::VInterleave128:
        case Opcode::VDeinterleave128: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            size_t per = 16 / sizeOfType(lane);
            for (size_t i = 0; i < n; ++i) {
                size_t g = i / per;
                size_t k = i % per;
                const Value& src = (g % 2 == 0) ? args[0] : args[1];
                vectorSetLane(out, lane, i, vectorGetLane(src, lane, (g / 2) * per + k));
            }
            return out;
        }
        case Opcode::VGetexp:
        case Opcode::VGetexppd: {
            for (size_t i = 0; i < n; ++i) {
                double d = vectorGetLane(args[0], lane, i).asDouble();
                int e = 0;
                std::frexp(d, &e);
                vectorSetLane(out, lane, i, laneFromDouble(lane, static_cast<double>(e - 1)));
            }
            return out;
        }
        case Opcode::VGetmant:
        case Opcode::VGetmantpd: {
            for (size_t i = 0; i < n; ++i) {
                double d = vectorGetLane(args[0], lane, i).asDouble();
                int e = 0;
                vectorSetLane(out, lane, i, laneFromDouble(lane, std::frexp(d, &e)));
            }
            return out;
        }
        case Opcode::VScalef:
        case Opcode::VScalefpd:
        case Opcode::VScalefps: {
            if (argCount < 2) throw RuntimeError("vscalef requires two vectors");
            for (size_t i = 0; i < n; ++i) {
                double d = vectorGetLane(args[0], lane, i).asDouble();
                int e = static_cast<int>(vectorGetLane(args[1], lane, i).asInt128());
                vectorSetLane(out, lane, i, laneFromDouble(lane, std::ldexp(d, e)));
            }
            return out;
        }
        case Opcode::VRange:
        case Opcode::VRangepd:
        case Opcode::VRangeps:
        case Opcode::VFixup: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            for (size_t i = 0; i < n; ++i) {
                double a = vectorGetLane(args[0], lane, i).asDouble();
                double b = vectorGetLane(args[1], lane, i).asDouble();
                vectorSetLane(out, lane, i, laneFromDouble(lane, a < b ? a : b));
            }
            return out;
        }
        case Opcode::VReducepd:
        case Opcode::VReduceps: {
            for (size_t i = 0; i < n; ++i) {
                double d = vectorGetLane(args[0], lane, i).asDouble();
                vectorSetLane(out, lane, i, laneFromDouble(lane, d - std::floor(d)));
            }
            return out;
        }
        case Opcode::VCvtpd2ps:
        case Opcode::VCvtps2pd:
        case Opcode::VCvtdq2pd:
        case Opcode::VCvtdq2ps: {
            for (size_t i = 0; i < n; ++i) {
                vectorSetLane(out, lane, i, laneFromDouble(lane, vectorGetLane(args[0], lane, i).asDouble()));
            }
            return out;
        }
        case Opcode::VCvtpd2dq:
        case Opcode::VCvtps2dq: {
            for (size_t i = 0; i < n; ++i) {
                vectorSetLane(out, lane, i,
                              laneFromInt(lane, static_cast<Int128>(vectorGetLane(args[0], lane, i).asDouble())));
            }
            return out;
        }
        case Opcode::VWiden256:
        case Opcode::VNarrow256: {
            for (size_t i = 0; i < n; ++i) {
                size_t src = (op == Opcode::VWiden256) ? i / 2 : (i * 2 < n ? i * 2 : n - 1);
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, src));
            }
            return out;
        }
        case Opcode::VShufi64:
        case Opcode::VShufi32: {
            if (argCount < 3) throw RuntimeError("this instruction requires two vectors and a selector");
            Int128 sel = args[2].asInt128();
            for (size_t i = 0; i < n; ++i) {
                size_t k = static_cast<size_t>((sel >> (i * 2)) & 0x3) % n;
                vectorSetLane(out, lane, i,
                              (i < n / 2) ? vectorGetLane(args[0], lane, k) : vectorGetLane(args[1], lane, k));
            }
            return out;
        }
        default:
            break;
    }

    throw RuntimeError(std::string("vector opcode not implemented: ") + opcodeName(op));
}

Value vectorComputeMasked(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args,
                          std::size_t argCount, bool zeroing) {
    size_t n = vectorLaneCount(width, lane);
    if (argCount < 2) throw RuntimeError(std::string(opcodeName(op)) + " requires a mask and operands");
    const Value& mask = args[0];
    Value out;
    out.type = width;
    if (!zeroing && argCount >= 2) {
        out = args[1];
        out.type = width;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!maskBit(mask, i)) {
            if (zeroing) vectorSetLane(out, lane, i, allZero(lane));
            continue;
        }
        Value lanes[3];
        size_t k = 0;
        for (size_t a = 1; a < argCount && k < 3; ++a, ++k) lanes[k] = vectorGetLane(args[a], lane, i);
        Value r;
        if (k >= 3) {
            double x = lanes[0].asDouble(), y = lanes[1].asDouble(), z = lanes[2].asDouble();
            r = isFloatLane(lane) ? laneFromDouble(lane, x * y + z)
                                   : laneFromInt(lane, lanes[0].asInt128() * lanes[1].asInt128() + lanes[2].asInt128());
        } else if (k == 2) {
            r = laneBinary(op, lane, lanes[0], lanes[1]);
        } else {
            r = laneUnary(op, lane, lanes[0]);
        }
        vectorSetLane(out, lane, i, r);
    }
    return out;
}

Value vectorCompute(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args, std::size_t argCount) {
    size_t n = vectorLaneCount(width, lane);
    Value out;
    out.type = width;

    switch (op) {
        case Opcode::VZero:
            return out;
        case Opcode::VOnes: {
            for (size_t i = 0; i < n; ++i) vectorSetLane(out, lane, i, allOnes(lane));
            return out;
        }
        case Opcode::VIota: {
            for (size_t i = 0; i < n; ++i) {
                vectorSetLane(out, lane, i,
                              isFloatLane(lane) ? laneFromDouble(lane, static_cast<double>(i))
                                                : laneFromInt(lane, static_cast<Int128>(i)));
            }
            return out;
        }
        case Opcode::VLen:
            return Value::fromInt128(Type::I32, static_cast<Int128>(n));
        default:
            break;
    }

    if (argCount == 0) throw RuntimeError(std::string(opcodeName(op)) + " requires at least one operand");

    switch (op) {
        case Opcode::VSplat:
        case Opcode::VBroadcast: {
            Value lv = isFloatLane(lane) ? laneFromDouble(lane, args[0].asDouble())
                                          : laneFromInt(lane, args[0].asInt128());
            for (size_t i = 0; i < n; ++i) vectorSetLane(out, lane, i, lv);
            return out;
        }
        case Opcode::VExtract: {
            if (argCount < 2) throw RuntimeError("vextract requires a lane index");
            Int128 idx = args[1].asInt128();
            if (idx < 0 || static_cast<size_t>(idx) >= n) throw RuntimeError("vector lane index out of range");
            return vectorGetLane(args[0], lane, static_cast<size_t>(idx));
        }
        case Opcode::VInsert: {
            if (argCount < 3) throw RuntimeError("vinsert requires a lane index and a value");
            Int128 idx = args[1].asInt128();
            if (idx < 0 || static_cast<size_t>(idx) >= n) throw RuntimeError("vector lane index out of range");
            out = args[0];
            out.type = width;
            Value lv = isFloatLane(lane) ? laneFromDouble(lane, args[2].asDouble())
                                          : laneFromInt(lane, args[2].asInt128());
            vectorSetLane(out, lane, static_cast<size_t>(idx), lv);
            return out;
        }
        case Opcode::VDupelem: {
            if (argCount < 2) throw RuntimeError("vdupelem requires a lane index");
            Int128 idx = args[1].asInt128();
            if (idx < 0 || static_cast<size_t>(idx) >= n) throw RuntimeError("vector lane index out of range");
            Value lv = vectorGetLane(args[0], lane, static_cast<size_t>(idx));
            for (size_t i = 0; i < n; ++i) vectorSetLane(out, lane, i, lv);
            return out;
        }
        case Opcode::VAnytrue:
        case Opcode::VAlltrue:
        case Opcode::VTestz: {
            bool any = false;
            bool all = true;
            for (size_t i = 0; i < n; ++i) {
                bool t = laneTruthy(vectorGetLane(args[0], lane, i), lane);
                if (t) any = true;
                else all = false;
            }
            if (op == Opcode::VAnytrue) return Value::fromInt128(Type::I32, any ? 1 : 0);
            if (op == Opcode::VAlltrue) return Value::fromInt128(Type::I32, all ? 1 : 0);
            return Value::fromInt128(Type::I32, any ? 0 : 1);
        }
        case Opcode::VBitmask: {
            Int128 mask = 0;
            for (size_t i = 0; i < n; ++i) {
                if (laneSignBit(vectorGetLane(args[0], lane, i), lane)) mask |= static_cast<Int128>(1) << i;
            }
            return Value::fromInt128(Type::I32, mask);
        }
        case Opcode::VCountones: {
            Int128 c = 0;
            for (size_t i = 0; i < n; ++i) {
                if (laneTruthy(vectorGetLane(args[0], lane, i), lane)) ++c;
            }
            return Value::fromInt128(Type::I32, c);
        }
        case Opcode::VIsnan: {
            for (size_t i = 0; i < n; ++i) {
                double d = vectorGetLane(args[0], lane, i).asDouble();
                vectorSetLane(out, lane, i, (d != d) ? allOnes(lane) : allZero(lane));
            }
            return out;
        }
        case Opcode::VSignbit: {
            for (size_t i = 0; i < n; ++i) {
                bool s = laneSignBit(vectorGetLane(args[0], lane, i), lane);
                vectorSetLane(out, lane, i, s ? allOnes(lane) : allZero(lane));
            }
            return out;
        }
        case Opcode::VReduceadd:
        case Opcode::VReducemul:
        case Opcode::VReducemin:
        case Opcode::VReducemax:
        case Opcode::VReduceand:
        case Opcode::VReduceor:
        case Opcode::VReducexor:
        case Opcode::VSum:
        case Opcode::VSum2: {
            if (isFloatLane(lane) &&
                (op == Opcode::VReduceadd || op == Opcode::VReducemul || op == Opcode::VReducemin ||
                 op == Opcode::VReducemax || op == Opcode::VSum || op == Opcode::VSum2)) {
                double acc = vectorGetLane(args[0], lane, 0).asDouble();
                for (size_t i = 1; i < n; ++i) {
                    double v = vectorGetLane(args[0], lane, i).asDouble();
                    if (op == Opcode::VReducemul) acc *= v;
                    else if (op == Opcode::VReducemin) acc = v < acc ? v : acc;
                    else if (op == Opcode::VReducemax) acc = v > acc ? v : acc;
                    else acc += v;
                }
                return laneFromDouble(lane, acc);
            }
            Int128 acc = vectorGetLane(args[0], lane, 0).asInt128();
            for (size_t i = 1; i < n; ++i) {
                Int128 v = vectorGetLane(args[0], lane, i).asInt128();
                switch (op) {
                    case Opcode::VReducemul: acc *= v; break;
                    case Opcode::VReducemin: acc = v < acc ? v : acc; break;
                    case Opcode::VReducemax: acc = v > acc ? v : acc; break;
                    case Opcode::VReduceand: acc &= v; break;
                    case Opcode::VReduceor: acc |= v; break;
                    case Opcode::VReducexor: acc ^= v; break;
                    default: acc += v; break;
                }
            }
            return laneFromInt(lane, acc);
        }
        case Opcode::VDot: {
            if (argCount < 2) throw RuntimeError("vdot requires two vectors");
            if (isFloatLane(lane)) {
                double acc = 0;
                for (size_t i = 0; i < n; ++i) {
                    acc += vectorGetLane(args[0], lane, i).asDouble() * vectorGetLane(args[1], lane, i).asDouble();
                }
                return laneFromDouble(lane, acc);
            }
            Int128 acc = 0;
            for (size_t i = 0; i < n; ++i) {
                acc += vectorGetLane(args[0], lane, i).asInt128() * vectorGetLane(args[1], lane, i).asInt128();
            }
            return laneFromInt(lane, acc);
        }
        case Opcode::VSelect:
        case Opcode::VBlend: {
            if (argCount < 3) throw RuntimeError("vselect requires a mask and two vectors");
            for (size_t i = 0; i < n; ++i) {
                bool t = laneTruthy(vectorGetLane(args[0], lane, i), lane);
                vectorSetLane(out, lane, i, vectorGetLane(args[t ? 1 : 2], lane, i));
            }
            return out;
        }
        case Opcode::VBitselect: {
            if (argCount < 3) throw RuntimeError("vbitselect requires three vectors");
            for (size_t i = 0; i < n; ++i) {
                UInt128 a = laneUnsigned(vectorGetLane(args[0], lane, i), lane);
                UInt128 b = laneUnsigned(vectorGetLane(args[1], lane, i), lane);
                UInt128 m = laneUnsigned(vectorGetLane(args[2], lane, i), lane);
                vectorSetLane(out, lane, i, laneFromInt(lane, static_cast<Int128>((a & m) | (b & ~m))));
            }
            return out;
        }
        case Opcode::VShuffle:
        case Opcode::VSwizzle: {
            if (argCount < 2) throw RuntimeError("vshuffle requires an index vector");
            const Value& src = args[0];
            const Value& idx = args[argCount - 1];
            bool twoSource = (op == Opcode::VShuffle && argCount >= 3);
            for (size_t i = 0; i < n; ++i) {
                Int128 k = vectorGetLane(idx, lane, i).asInt128();
                if (k < 0) {
                    vectorSetLane(out, lane, i, allZero(lane));
                    continue;
                }
                size_t kk = static_cast<size_t>(k);
                if (twoSource) {
                    if (kk < n) vectorSetLane(out, lane, i, vectorGetLane(src, lane, kk));
                    else if (kk < 2 * n) vectorSetLane(out, lane, i, vectorGetLane(args[1], lane, kk - n));
                    else vectorSetLane(out, lane, i, allZero(lane));
                } else {
                    vectorSetLane(out, lane, i, kk < n ? vectorGetLane(src, lane, kk) : allZero(lane));
                }
            }
            return out;
        }
        case Opcode::VReverse: {
            for (size_t i = 0; i < n; ++i) vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, n - 1 - i));
            return out;
        }
        case Opcode::VRotate: {
            if (argCount < 2) throw RuntimeError("vrotate requires a lane count");
            Int128 k = args[1].asInt128();
            for (size_t i = 0; i < n; ++i) {
                Int128 from = (static_cast<Int128>(i) + k) % static_cast<Int128>(n);
                if (from < 0) from += static_cast<Int128>(n);
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, static_cast<size_t>(from)));
            }
            return out;
        }
        case Opcode::VShiftin: {
            if (argCount < 2) throw RuntimeError("vshiftin requires a value");
            for (size_t i = 0; i + 1 < n; ++i) vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i + 1));
            Value lv = isFloatLane(lane) ? laneFromDouble(lane, args[1].asDouble())
                                          : laneFromInt(lane, args[1].asInt128());
            vectorSetLane(out, lane, n - 1, lv);
            return out;
        }
        case Opcode::VShiftout: {
            for (size_t i = n; i-- > 1;) vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i - 1));
            vectorSetLane(out, lane, 0, allZero(lane));
            return out;
        }
        case Opcode::VZip:
        case Opcode::VUnpacklo:
        case Opcode::VUnpackhi: {
            if (argCount < 2) throw RuntimeError("this instruction requires two vectors");
            size_t base = (op == Opcode::VUnpackhi) ? n / 2 : 0;
            for (size_t i = 0; i < n / 2; ++i) {
                vectorSetLane(out, lane, i * 2, vectorGetLane(args[0], lane, base + i));
                vectorSetLane(out, lane, i * 2 + 1, vectorGetLane(args[1], lane, base + i));
            }
            return out;
        }
        case Opcode::VUnzip: {
            if (argCount < 2) throw RuntimeError("vunzip requires two vectors");
            for (size_t i = 0; i < n / 2; ++i) {
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i * 2));
                vectorSetLane(out, lane, n / 2 + i, vectorGetLane(args[1], lane, i * 2));
            }
            return out;
        }
        case Opcode::VConcat: {
            if (argCount < 2) throw RuntimeError("vconcat requires two vectors");
            for (size_t i = 0; i < n / 2; ++i) {
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, i));
                vectorSetLane(out, lane, n / 2 + i, vectorGetLane(args[1], lane, i));
            }
            return out;
        }
        case Opcode::VWidenlo:
        case Opcode::VWidenhi:
        case Opcode::VExtend: {
            size_t base = (op == Opcode::VWidenhi) ? n / 2 : 0;
            for (size_t i = 0; i < n; ++i) {
                size_t src = base + i / 2;
                vectorSetLane(out, lane, i, vectorGetLane(args[0], lane, src < n ? src : n - 1));
            }
            return out;
        }
        case Opcode::VNarrow:
        case Opcode::VPacks:
        case Opcode::VPacku: {
            size_t bits = laneBits(lane);
            Int128 lo = laneSignedMin(bits);
            Int128 hi = laneSignedMax(bits);
            for (size_t i = 0; i < n; ++i) {
                const Value& srcVec = (i < n / 2 || argCount < 2) ? args[0] : args[1];
                size_t si = (i < n / 2) ? i * 2 : (i - n / 2) * 2;
                if (si >= n) si = n - 1;
                Value v = vectorGetLane(srcVec, lane, si);
                if (op == Opcode::VPacks) {
                    Int128 x = v.asInt128();
                    if (x > hi) x = hi;
                    if (x < lo) x = lo;
                    v = laneFromInt(lane, x);
                } else if (op == Opcode::VPacku) {
                    Int128 x = v.asInt128();
                    if (x < 0) x = 0;
                    v = laneFromInt(lane, x);
                }
                vectorSetLane(out, lane, i, v);
            }
            return out;
        }
        case Opcode::VConvert:
        case Opcode::VCvtfp:
        case Opcode::VCvtint: {
            for (size_t i = 0; i < n; ++i) {
                Value v = vectorGetLane(args[0], lane, i);
                if (op == Opcode::VCvtfp) vectorSetLane(out, lane, i, laneFromDouble(lane, v.asDouble()));
                else if (op == Opcode::VCvtint) vectorSetLane(out, lane, i, laneFromInt(lane, v.asInt128()));
                else vectorSetLane(out, lane, i, v);
            }
            return out;
        }
        case Opcode::VClamp: {
            if (argCount < 3) throw RuntimeError("vclamp requires a low and high vector");
            for (size_t i = 0; i < n; ++i) {
                Value v = vectorGetLane(args[0], lane, i);
                Value l = vectorGetLane(args[1], lane, i);
                Value h = vectorGetLane(args[2], lane, i);
                if (isFloatLane(lane)) {
                    double x = v.asDouble(), a = l.asDouble(), b = h.asDouble();
                    vectorSetLane(out, lane, i, laneFromDouble(lane, x < a ? a : (x > b ? b : x)));
                } else {
                    Int128 x = v.asInt128(), a = l.asInt128(), b = h.asInt128();
                    vectorSetLane(out, lane, i, laneFromInt(lane, x < a ? a : (x > b ? b : x)));
                }
            }
            return out;
        }
        case Opcode::VLerp: {
            if (argCount < 3) throw RuntimeError("vlerp requires three operands");
            for (size_t i = 0; i < n; ++i) {
                double a = vectorGetLane(args[0], lane, i).asDouble();
                double b = vectorGetLane(args[1], lane, i).asDouble();
                double t = vectorGetLane(args[2], lane, i).asDouble();
                vectorSetLane(out, lane, i, laneFromDouble(lane, a + t * (b - a)));
            }
            return out;
        }
        case Opcode::VMla:
        case Opcode::VMls:
        case Opcode::VMadd:
        case Opcode::VMsub: {
            if (argCount < 3) throw RuntimeError("this instruction requires three vectors");
            for (size_t i = 0; i < n; ++i) {
                if (isFloatLane(lane)) {
                    double a = vectorGetLane(args[0], lane, i).asDouble();
                    double b = vectorGetLane(args[1], lane, i).asDouble();
                    double c = vectorGetLane(args[2], lane, i).asDouble();
                    double r = (op == Opcode::VMla || op == Opcode::VMadd) ? (a * b + c) : (c - a * b);
                    vectorSetLane(out, lane, i, laneFromDouble(lane, r));
                } else {
                    Int128 a = vectorGetLane(args[0], lane, i).asInt128();
                    Int128 b = vectorGetLane(args[1], lane, i).asInt128();
                    Int128 c = vectorGetLane(args[2], lane, i).asInt128();
                    Int128 r = (op == Opcode::VMla || op == Opcode::VMadd) ? (a * b + c) : (c - a * b);
                    vectorSetLane(out, lane, i, laneFromInt(lane, r));
                }
            }
            return out;
        }
        case Opcode::VMin3:
        case Opcode::VMax3: {
            if (argCount < 3) throw RuntimeError("this instruction requires three vectors");
            for (size_t i = 0; i < n; ++i) {
                Value r = vectorGetLane(args[0], lane, i);
                for (size_t k = 1; k < 3; ++k) {
                    Value v = vectorGetLane(args[k], lane, i);
                    bool takeV = (op == Opcode::VMin3) ? (isFloatLane(lane) ? v.asDouble() < r.asDouble()
                                                                            : v.asInt128() < r.asInt128())
                                                        : (isFloatLane(lane) ? v.asDouble() > r.asDouble()
                                                                            : v.asInt128() > r.asInt128());
                    if (takeV) r = v;
                }
                vectorSetLane(out, lane, i, r);
            }
            return out;
        }
        case Opcode::VCmpeq:
        case Opcode::VCmpne:
        case Opcode::VCmplt:
        case Opcode::VCmpgt:
        case Opcode::VCmple:
        case Opcode::VCmpge:
        case Opcode::VUcmplt:
        case Opcode::VUcmpgt:
        case Opcode::VUcmple:
        case Opcode::VUcmpge:
        case Opcode::VCmpord:
        case Opcode::VCmpuno: {
            if (argCount < 2) throw RuntimeError("vector comparison requires two vectors");
            for (size_t i = 0; i < n; ++i) {
                bool t = laneCompare(op, lane, vectorGetLane(args[0], lane, i), vectorGetLane(args[1], lane, i));
                vectorSetLane(out, lane, i, t ? allOnes(lane) : allZero(lane));
            }
            return out;
        }
        default:
            break;
    }

    if (argCount >= 2) {
        for (size_t i = 0; i < n; ++i) {
            vectorSetLane(out, lane, i,
                          laneBinary(op, lane, vectorGetLane(args[0], lane, i), vectorGetLane(args[1], lane, i)));
        }
        return out;
    }

    for (size_t i = 0; i < n; ++i) {
        vectorSetLane(out, lane, i, laneUnary(op, lane, vectorGetLane(args[0], lane, i)));
    }
    return out;
}

}
