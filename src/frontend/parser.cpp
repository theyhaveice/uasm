#include "uasm/parser.h"

#include "uasm/compat.h"
#include "uasm/lexer.h"
#include "uasm/opcode_info.h"

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif

namespace uasm {

namespace {

#if __cplusplus >= 201103L
typedef std::unordered_map<std::string, uint32_t> ParamMap;
#else
typedef std::map<std::string, uint32_t> ParamMap;
#endif


class Parser {
public:
    Parser(std::vector<Token> tokens, std::string filename, const ExtensionSet& enabled)
        : tokens_(UASM_MOVE(tokens)), filename_(UASM_MOVE(filename)), pos_(0), enabled_(enabled) {}

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
    ExtensionSet enabled_;
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

        Opcode::Value op = Opcode::Mov;
        if (!opcodeFromName(mnemonic, op)) fail("unknown instruction '" + mnemonic + "'");
        Extension::Value ext = opcodeExtension(op);
        if (!enabled_.has(ext)) {
            fail("instruction '" + mnemonic + "' needs the '" + extensionName(ext) +
                 "' extension (add --enable-" + extensionName(ext) + ")");
        }
        instr.opcode = op;

        if (takesTypeSuffix(instr.opcode)) {
            expectAny(TokenKind::Dot);
            instr.type = expectType();
        }

        if (takesSecondTypeSuffix(instr.opcode)) {
            expectAny(TokenKind::Dot);
            instr.type2 = expectType();
        } else {
            instr.type2 = instr.type;
        }

        if (takesNoOperands(instr.opcode)) return instr;

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

Module parseModule(const std::string& source, const std::string& filename, const ExtensionSet& enabled) {
    std::vector<Token> tokens;
    try {
        tokens = lex(source);
    } catch (const LexError& e) {
        throw ParseError(filename + ": " + e.message, e.line);
    }
    Parser parser(UASM_MOVE(tokens), filename, enabled);
    return parser.parse();
}

Module parseModule(const std::string& source, const std::string& filename) {
    return parseModule(source, filename, ExtensionSet());
}

}
