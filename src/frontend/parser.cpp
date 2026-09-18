#include "uasm/parser.h"

#include "uasm/compat.h"
#include "uasm/lexer.h"

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif

namespace uasm {

namespace {

#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, Opcode::Value> MnemonicMap;
typedef std::unordered_map<std::string, uint32_t> ParamMap;
#else
typedef std::map<std::string, Opcode::Value> MnemonicMap;
typedef std::map<std::string, uint32_t> ParamMap;
#endif

const MnemonicMap& mnemonics() {
    static MnemonicMap m;
    if (m.empty()) {
        m["mov"] = Opcode::Mov;
        m["add"] = Opcode::Add;
        m["sub"] = Opcode::Sub;
        m["mul"] = Opcode::Mul;
        m["div"] = Opcode::Div;
        m["cmp"] = Opcode::Cmp;
        m["beq"] = Opcode::Beq;
        m["bne"] = Opcode::Bne;
        m["blt"] = Opcode::Blt;
        m["bgt"] = Opcode::Bgt;
        m["ble"] = Opcode::Ble;
        m["bge"] = Opcode::Bge;
        m["jmp"] = Opcode::Jmp;
        m["call"] = Opcode::Call;
        m["ret"] = Opcode::Ret;
        m["load"] = Opcode::Load;
        m["store"] = Opcode::Store;
        m["and"] = Opcode::And;
        m["or"] = Opcode::Or;
        m["xor"] = Opcode::Xor;
        m["not"] = Opcode::Not;
        m["shl"] = Opcode::Shl;
        m["shr"] = Opcode::Shr;
        m["mod"] = Opcode::Mod;
        m["neg"] = Opcode::Neg;
        m["push"] = Opcode::Push;
        m["pop"] = Opcode::Pop;
        m["convert"] = Opcode::Convert;
        m["cast"] = Opcode::Cast;
    }
    return m;
}

bool takesTypeSuffix(Opcode::Value op) {
    switch (op) {
        case Opcode::Mov:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Cmp:
        case Opcode::Ret:
        case Opcode::Load:
        case Opcode::Store:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Not:
        case Opcode::Shl:
        case Opcode::Shr:
        case Opcode::Mod:
        case Opcode::Neg:
        case Opcode::Push:
        case Opcode::Pop:
        case Opcode::Convert:
        case Opcode::Cast:
            return true;
        default:
            return false;
    }
}

bool hasDestOperand(Opcode::Value op) {
    switch (op) {
        case Opcode::Mov:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Load:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Not:
        case Opcode::Shl:
        case Opcode::Shr:
        case Opcode::Mod:
        case Opcode::Neg:
        case Opcode::Pop:
        case Opcode::Convert:
        case Opcode::Cast:
            return true;
        default:
            return false;
    }
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string filename)
        : tokens_(UASM_MOVE(tokens)), filename_(UASM_MOVE(filename)), pos_(0) {}

    Module parse() {
        Module mod;
        expectIdent("module");
        mod.name = expectAny(TokenKind::Identifier).text;

        while (!check(TokenKind::End)) {
            Function fn = parseFunction();
            fn.module = mod.name;
            mod.functions.push_back(UASM_MOVE(fn));
        }
        return mod;
    }

private:
    std::vector<Token> tokens_;
    std::string filename_;
    size_t pos_;
    ParamMap currentParams_;

    const Token& cur() const { return tokens_[pos_]; }
    bool check(TokenKind::Value k) const { return cur().kind == k; }
    bool checkIdent(const std::string& s) const {
        return cur().kind == TokenKind::Identifier && cur().text == s;
    }

    const Token& advance() {
        const Token& t = tokens_[pos_];
        if (pos_ + 1 < tokens_.size()) ++pos_;
        return t;
    }

    void fail(const std::string& msg) {
        throw ParseError(filename_ + ": " + msg, cur().line);
    }

    const Token& expectAny(TokenKind::Value k) {
        if (!check(k)) fail("unexpected token '" + cur().text + "'");
        return advance();
    }

    void expectIdent(const std::string& s) {
        if (!checkIdent(s)) fail("expected '" + s + "'");
        advance();
    }

    Type::Value expectType() {
        const Token& t = expectAny(TokenKind::Identifier);
        Type::Value ty = Type::I32;
        if (!typeFromName(t.text, ty)) fail("unknown type '" + t.text + "'");
        return ty;
    }

    Function parseFunction() {
        Function fn;
        if (checkIdent("export")) {
            fn.isExported = true;
            advance();
        }
        expectIdent("func");
        fn.name = expectAny(TokenKind::Identifier).text;

        expectAny(TokenKind::LParen);
        if (!check(TokenKind::RParen)) {
            for (;;) {
                Param p;
                p.name = expectAny(TokenKind::Identifier).text;
                expectAny(TokenKind::Colon);
                p.type = expectType();
                fn.params.push_back(p);
                if (!check(TokenKind::Comma)) break;
                advance();
            }
        }
        expectAny(TokenKind::RParen);
        expectAny(TokenKind::Arrow);
        fn.returnType = expectType();

        currentParams_.clear();
        for (uint32_t i = 0; i < fn.params.size(); ++i) currentParams_[fn.params[i].name] = i;

        expectAny(TokenKind::LBrace);
        while (!check(TokenKind::RBrace)) {
            fn.blocks.push_back(parseBlock());
        }
        expectAny(TokenKind::RBrace);
        currentParams_.clear();
        return fn;
    }

    Block parseBlock() {
        Block b;
        b.label = expectAny(TokenKind::Identifier).text;
        expectAny(TokenKind::Colon);
        while (check(TokenKind::Identifier) && !isBlockTerminator()) {
            b.instructions.push_back(parseInstruction());
        }
        return b;
    }

    bool isBlockTerminator() const {
        return pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::Colon;
    }

    Instruction parseInstruction() {
        Instruction instr;
        instr.line = cur().line;
        std::string mnemonic = expectAny(TokenKind::Identifier).text;

        if (mnemonic == "ret" && !check(TokenKind::Dot)) {
            instr.opcode = Opcode::RetVoid;
            return instr;
        }

        MnemonicMap::const_iterator it = mnemonics().find(mnemonic);
        if (it == mnemonics().end()) fail("unknown instruction '" + mnemonic + "'");
        instr.opcode = it->second;

        if (takesTypeSuffix(instr.opcode)) {
            expectAny(TokenKind::Dot);
            instr.type = expectType();
        }

        bool destIsRegister = hasDestOperand(instr.opcode);
        bool first = true;
        while (first ? isOperandStart() : check(TokenKind::Comma)) {
            if (!first) expectAny(TokenKind::Comma);
            first = false;
            Operand op = parseOperand();
            if (destIsRegister && !instr.hasDest) {
                if (op.kind != Operand::Register) fail("expected destination register");
                instr.hasDest = true;
                instr.dest = op.reg;
            } else {
                instr.operands.push_back(op);
            }
        }

        if (instr.opcode == Opcode::Call) {

            if (instr.operands.empty() || instr.operands[0].kind != Operand::Symbol) {
                fail("call requires a function name");
            }
            if (instr.operands.size() >= 2) {
                if (instr.operands[1].kind != Operand::Register) fail("call destination must be a register");
                instr.hasDest = true;
                instr.dest = instr.operands[1].reg;
                instr.operands.erase(instr.operands.begin() + 1);
            }
        }

        return instr;
    }

    bool isOperandStart() const {
        switch (cur().kind) {
            case TokenKind::Register:
            case TokenKind::IntLiteral:
            case TokenKind::FloatLiteral:
            case TokenKind::Identifier:
            case TokenKind::ParamRef:
                return true;
            default:
                return false;
        }
    }

    Operand parseOperand() {
        Operand op;
        if (check(TokenKind::Register)) {
            op.kind = Operand::Register;
            op.reg = static_cast<uint32_t>(advance().intValue);
        } else if (check(TokenKind::IntLiteral)) {
            op.kind = Operand::ImmediateInt;
            op.immInt = advance().intValue;
        } else if (check(TokenKind::FloatLiteral)) {
            op.kind = Operand::ImmediateFloat;
            op.immFloat = advance().floatValue;
        } else if (check(TokenKind::Identifier)) {

            const Token& t = advance();
            op.kind = Operand::Symbol;
            op.name = t.text;
        } else if (check(TokenKind::ParamRef)) {
            const Token& t = advance();
            ParamMap::const_iterator it = currentParams_.find(t.text);
            if (it == currentParams_.end()) {
                fail("unknown parameter '$" + t.text + "' (not a parameter of the enclosing function)");
            }
            op.kind = Operand::Register;
            op.reg = it->second;
        } else {
            fail("expected operand");
        }
        return op;
    }
};

}

Module parseModule(const std::string& source, const std::string& filename) {
    std::vector<Token> tokens;
    try {
        tokens = lex(source);
    } catch (const LexError& e) {
        throw ParseError(filename + ": " + e.message, e.line);
    }
    Parser parser(UASM_MOVE(tokens), filename);
    return parser.parse();
}

}
