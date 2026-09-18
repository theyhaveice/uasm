#include "uasm/lexer.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace uasm {

namespace {

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

Token makeToken(TokenKind::Value kind, const std::string& text, int line) {
    Token t;
    t.kind = kind;
    t.text = text;
    t.line = line;
    return t;
}

}

std::vector<Token> lex(const std::string& src) {
    std::vector<Token> tokens;
    size_t i = 0;
    int line = 1;
    const size_t n = src.size();

    while (i < n) {
        char c = src[i];

        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == ';') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1]))) {

            size_t start = i;
            ++i;
            while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            bool isFloat = false;
            if (i < n && src[i] == '.') {
                isFloat = true;
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            }
            std::string text = src.substr(start, i - start);
            Token t;
            t.line = line;
            t.text = text;
            if (isFloat) {
                t.kind = TokenKind::FloatLiteral;
                t.floatValue = std::strtod(text.c_str(), 0);
            } else {
                t.kind = TokenKind::IntLiteral;
                t.intValue = std::strtoll(text.c_str(), 0, 10);
            }
            tokens.push_back(t);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            bool isFloat = false;
            if (i < n && src[i] == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1]))) {
                isFloat = true;
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            }
            std::string text = src.substr(start, i - start);
            Token t;
            t.line = line;
            t.text = text;
            if (isFloat) {
                t.kind = TokenKind::FloatLiteral;
                t.floatValue = std::strtod(text.c_str(), 0);
            } else {
                t.kind = TokenKind::IntLiteral;
                t.intValue = std::strtoll(text.c_str(), 0, 10);
            }
            tokens.push_back(t);
            continue;
        }
        if (c == 'r' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1]))) {
            size_t start = i;
            ++i;
            while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            Token t;
            t.kind = TokenKind::Register;
            t.text = src.substr(start, i - start);
            t.intValue = std::strtoll(t.text.c_str() + 1, 0, 10);
            t.line = line;
            tokens.push_back(t);
            continue;
        }
        if (isIdentStart(c)) {
            size_t start = i;
            while (i < n && isIdentChar(src[i])) ++i;
            Token t;
            t.kind = TokenKind::Identifier;
            t.text = src.substr(start, i - start);
            t.line = line;
            tokens.push_back(t);
            continue;
        }
        if (c == '-' && i + 1 < n && src[i + 1] == '>') {
            tokens.push_back(makeToken(TokenKind::Arrow, "->", line));
            i += 2;
            continue;
        }
        if (c == '$') {
            ++i;
            size_t nameStart = i;
            while (i < n && isIdentChar(src[i])) ++i;
            std::string name = src.substr(nameStart, i - nameStart);
            if (name.empty() || !isIdentStart(name[0])) {
                throw LexError("expected a parameter name after '$'", line);
            }
            Token t;
            t.kind = TokenKind::ParamRef;
            t.text = name;
            t.line = line;
            tokens.push_back(t);
            continue;
        }

        switch (c) {
            case ':': tokens.push_back(makeToken(TokenKind::Colon, ":", line)); ++i; continue;
            case ',': tokens.push_back(makeToken(TokenKind::Comma, ",", line)); ++i; continue;
            case '(': tokens.push_back(makeToken(TokenKind::LParen, "(", line)); ++i; continue;
            case ')': tokens.push_back(makeToken(TokenKind::RParen, ")", line)); ++i; continue;
            case '{': tokens.push_back(makeToken(TokenKind::LBrace, "{", line)); ++i; continue;
            case '}': tokens.push_back(makeToken(TokenKind::RBrace, "}", line)); ++i; continue;
            case '.': tokens.push_back(makeToken(TokenKind::Dot, ".", line)); ++i; continue;
            case '-': tokens.push_back(makeToken(TokenKind::Minus, "-", line)); ++i; continue;
            default:
                throw LexError(std::string("unexpected character '") + c + "'", line);
        }
    }

    tokens.push_back(makeToken(TokenKind::End, "", line));
    return tokens;
}

}
