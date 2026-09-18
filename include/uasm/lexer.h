#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uasm {

namespace TokenKind {
enum Value {
    Identifier,
    Register,
    ParamRef,
    IntLiteral,
    FloatLiteral,
    Arrow,
    Colon,
    Comma,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Dot,
    Minus,
    End
};
}

struct Token {
    TokenKind::Value kind;
    std::string text;
    int64_t intValue;
    double floatValue;
    int line;

    Token() : kind(TokenKind::End), intValue(0), floatValue(0.0), line(1) {}
};

struct LexError {
    std::string message;
    int line;
    LexError(const std::string& m, int l) : message(m), line(l) {}
};

std::vector<Token> lex(const std::string& source);

}
