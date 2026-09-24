#pragma once

#include "tokens.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace xpln {

// ---------------------------------------------------------------------------
// LexerError
// ---------------------------------------------------------------------------

class LexerError : public std::runtime_error {
public:
    LexerError(std::string_view message, std::uint32_t line, std::uint32_t col)
        : std::runtime_error(std::format("Lexer error at line {}, col {}: {}", line, col, message))
        , line_(line), col_(col) {}

    [[nodiscard]] constexpr std::uint32_t line() const noexcept { return line_; }
    [[nodiscard]] constexpr std::uint32_t col() const noexcept { return col_; }

private:
    std::uint32_t line_;
    std::uint32_t col_;
};

// ---------------------------------------------------------------------------
// Branchless ASCII character classification
// ---------------------------------------------------------------------------

namespace detail {

enum : std::uint8_t {
    kDigit      = 1u << 0,
    kAlpha      = 1u << 1,
    kIdentExtra = 1u << 2, // _ $ # @
};

[[nodiscard]] consteval std::array<std::uint8_t, 256> make_char_class() noexcept {
    std::array<std::uint8_t, 256> t{};
    for (int c = 0; c < 256; ++c) {
        std::uint8_t f = 0;
        if (c >= '0' && c <= '9') f |= kDigit;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) f |= kAlpha;
        if (c == '_' || c == '$' || c == '#' || c == '@') f |= kIdentExtra;
        t[static_cast<std::size_t>(c)] = f;
    }
    return t;
}
inline constexpr auto kCharClass = make_char_class();

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return (kCharClass[static_cast<unsigned char>(c)] & kDigit) != 0;
}
[[nodiscard]] constexpr bool is_ident_start(char c) noexcept {
    return (kCharClass[static_cast<unsigned char>(c)] & (kAlpha | kIdentExtra)) != 0;
}
[[nodiscard]] constexpr bool is_ident_cont(char c) noexcept {
    return (kCharClass[static_cast<unsigned char>(c)] & (kAlpha | kDigit | kIdentExtra)) != 0;
}
[[nodiscard]] constexpr bool is_alnum_or_underscore(char c) noexcept {
    return (kCharClass[static_cast<unsigned char>(c)] & (kAlpha | kDigit)) != 0 || c == '_';
}
[[nodiscard]] constexpr bool is_lower(char c) noexcept {
    return c >= 'a' && c <= 'z';
}
[[nodiscard]] constexpr char to_upper(char c) noexcept {
    return is_lower(c) ? static_cast<char>(c - ('a' - 'A')) : c;
}

} // namespace detail

// ---------------------------------------------------------------------------
// LexResult
// ---------------------------------------------------------------------------

struct LexResult {
    std::vector<Token> tokens;
    std::vector<char>  arena; // backing storage for rewritten lexemes
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

class Lexer {
public:
    explicit constexpr Lexer(std::string_view source) noexcept
        : source_(source), length_(source.size()) {}

    [[nodiscard]] constexpr char peek(std::size_t offset = 0) const noexcept {
        const std::size_t idx = pos_ + offset;
        return idx < length_ ? source_[idx] : '\0';
    }

    constexpr char advance() noexcept {
        const char ch = peek();
        ++pos_;
        if (ch == '\n') { ++line_; col_ = 1; }
        else            { ++col_; }
        return ch;
    }

    // Present for API parity with the Python lexer; unused by tokenize().
    constexpr bool match(std::string_view expected) noexcept {
        if (source_.substr(pos_, expected.size()) != expected) return false;
        for (std::size_t i = 0; i < expected.size(); ++i) advance();
        return true;
    }

    void skip_whitespace_and_comments() {
        while (pos_ < length_) {
            const char ch = peek();
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') [[likely]] {
                advance();
            } else if (ch == '/' && peek(1) == '*') {
                const auto start_line = line_, start_col = col_;
                advance(); advance(); // "/*"
                bool closed = false;
                while (pos_ < length_) {
                    if (peek() == '*' && peek(1) == '/') { advance(); advance(); closed = true; break; }
                    advance();
                }
                if (!closed) throw LexerError("Unterminated comment /*", start_line, start_col);
            } else if (ch == '/' && peek(1) == '/') {
                advance(); advance(); // "//"
                while (pos_ < length_ && peek() != '\n') advance();
            } else {
                break;
            }
        }
    }

    [[nodiscard]] LexResult tokenize() {
        LexResult result;
        result.tokens.reserve(length_ / 3 + 16); // heuristic; avoids most reallocations
        result.arena.reserve(length_);           // proven upper bound; never reallocated

        for (;;) {
            skip_whitespace_and_comments();
            if (pos_ >= length_) {
                result.tokens.emplace_back(TokenType::Eof, std::string_view{}, line_, col_);
                break;
            }

            const std::uint32_t start_line = line_, start_col = col_;
            const char ch = peek();

            switch (ch) {
            // ---- multi-character operators (with single-character fallback) ----
            case '*':
                if (peek(1) == '*') { advance(); advance(); emit(result, TokenType::Power, "**", start_line, start_col); }
                else                { advance();            emit(result, TokenType::Star,  "*",  start_line, start_col); }
                break;
            case '|':
                if (peek(1) == '|') { advance(); advance(); emit(result, TokenType::Concat, "||", start_line, start_col); }
                else                { advance();            emit(result, TokenType::Or,     "|",  start_line, start_col); }
                break;
            case '^': case '\\': case '~': case '!':
                if (peek(1) == '=') { advance(); advance(); emit(result, TokenType::Ne,  "^=", start_line, start_col); }
                else                { advance();            emit(result, TokenType::Not, "^",  start_line, start_col); }
                break;
            case '<':
                if      (peek(1) == '=') { advance(); advance(); emit(result, TokenType::Le, "<=", start_line, start_col); }
                else if (peek(1) == '>') { advance(); advance(); emit(result, TokenType::Ne, "^=", start_line, start_col); }
                else                     { advance();            emit(result, TokenType::Lt, "<",  start_line, start_col); }
                break;
            case '>':
                if (peek(1) == '=') { advance(); advance(); emit(result, TokenType::Ge, ">=", start_line, start_col); }
                else                { advance();            emit(result, TokenType::Gt, ">",  start_line, start_col); }
                break;

            // ---- single-character operators & delimiters ----
            case '+': advance(); emit(result, TokenType::Plus,      "+", start_line, start_col); break;
            case '-': advance(); emit(result, TokenType::Minus,     "-", start_line, start_col); break;
            case '/': advance(); emit(result, TokenType::Slash,     "/", start_line, start_col); break;
            case '=': advance(); emit(result, TokenType::Assign,    "=", start_line, start_col); break;
            case '&': advance(); emit(result, TokenType::And,       "&", start_line, start_col); break;
            case ';': advance(); emit(result, TokenType::Semicolon, ";", start_line, start_col); break;
            case ':': advance(); emit(result, TokenType::Colon,     ":", start_line, start_col); break;
            case ',': advance(); emit(result, TokenType::Comma,     ",", start_line, start_col); break;
            case '(': advance(); emit(result, TokenType::LParen,    "(", start_line, start_col); break;
            case ')': advance(); emit(result, TokenType::RParen,    ")", start_line, start_col); break;

            // ---- string / bit literals ----
            case '\'':
                lex_string(result, start_line, start_col);
                break;

            // ---- numbers, identifiers/keywords, error ----
            default:
                if (detail::is_digit(ch) || (ch == '.' && detail::is_digit(peek(1)))) {
                    lex_number(result, start_line, start_col);
                } else if (detail::is_ident_start(ch)) [[likely]] {
                    lex_identifier(result, start_line, start_col);
                } else {
                    throw LexerError(std::format("Unexpected character: '{}'", ch), start_line, start_col);
                }
            }
        }

        return result;
    }

private:
    static void emit(LexResult& r, TokenType type, std::string_view value,
                      std::uint32_t line, std::uint32_t col) {
        r.tokens.emplace_back(type, value, line, col);
    }

    // String literal: '...' with '' as an escaped single quote, optionally
    // followed by a B/b suffix marking it as a bit literal.
    void lex_string(LexResult& r, std::uint32_t start_line, std::uint32_t start_col) {
        advance(); // opening '
        const std::size_t content_start = pos_;
        std::size_t arena_start = 0;
        bool copied = false; // becomes true the first time we see a '' escape

        for (;;) {
            if (pos_ >= length_) throw LexerError("Unterminated string literal", start_line, start_col);
            const char c = advance();
            if (c == '\'') {
                if (peek() == '\'') {
                    if (!copied) {
                        // First escape in this literal: flush the plain
                        // prefix scanned so far into the arena, then
                        // continue appending there.
                        arena_start = r.arena.size();
                        const std::size_t raw_len = (pos_ - 1) - content_start;
                        r.arena.insert(r.arena.end(),
                                       source_.data() + content_start,
                                       source_.data() + content_start + raw_len);
                        copied = true;
                    }
                    r.arena.push_back('\'');
                    advance(); // consume the second '
                } else {
                    break; // unescaped ' terminates the literal
                }
            } else if (copied) {
                r.arena.push_back(c);
            }
        }

        const std::string_view string_val = copied
            ? std::string_view(r.arena.data() + arena_start, r.arena.size() - arena_start)
            : source_.substr(content_start, (pos_ - 1) - content_start);

        if ((peek() == 'B' || peek() == 'b') && !detail::is_alnum_or_underscore(peek(1))) {
            advance(); // consume suffix
            emit_bit_literal(r, string_val, start_line, start_col);
        } else {
            emit(r, TokenType::StringLit, string_val, start_line, start_col);
        }
    }

    // Validates `raw` is (once spaces are stripped) all '0'/'1', stripping
    // spaces via the same copy-on-first-need strategy as lex_string.
    void emit_bit_literal(LexResult& r, std::string_view raw, std::uint32_t line, std::uint32_t col) {
        std::size_t arena_start = 0;
        bool copied = false;

        for (std::size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c == ' ') {
                if (!copied) {
                    arena_start = r.arena.size();
                    r.arena.insert(r.arena.end(), raw.data(), raw.data() + i);
                    copied = true;
                }
                // spaces are dropped, not copied
            } else {
                if (c != '0' && c != '1') {
                    throw LexerError(std::format("Invalid bit string content '{}'", raw), line, col);
                }
                if (copied) r.arena.push_back(c);
            }
        }

        const std::string_view bits = copied
            ? std::string_view(r.arena.data() + arena_start, r.arena.size() - arena_start)
            : raw;
        emit(r, TokenType::BitLit, bits, line, col);
    }

    // Numeric literal (int or float, with optional fractional part and
    // exponent). Never requires rewriting, so it always views the source
    // directly.
    void lex_number(LexResult& r, std::uint32_t start_line, std::uint32_t start_col) {
        const std::size_t start = pos_;
        bool is_float = false;

        if (peek() == '.') {
            is_float = true;
            advance();
        }
        while (detail::is_digit(peek())) advance();

        if (peek() == '.' && !is_float) {
            is_float = true;
            advance();
            while (detail::is_digit(peek())) advance();
        }

        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!detail::is_digit(peek())) throw LexerError("Exponent has no digits", start_line, start_col);
            while (detail::is_digit(peek())) advance();
        }

        const std::string_view text = source_.substr(start, pos_ - start);
        emit(r, is_float ? TokenType::FloatLit : TokenType::IntLit, text, start_line, start_col);
    }

    // Identifier or reserved keyword. The source language is case-insensitive:
    // every identifier is upper-cased before classification/storage, with a
    // fast path that avoids the arena entirely when the source text is
    // already all upper-case (the common case for keyword-heavy PL/I-style
    // source).
    void lex_identifier(LexResult& r, std::uint32_t start_line, std::uint32_t start_col) {
        const std::size_t start = pos_;
        bool has_lower = false;
        while (detail::is_ident_cont(peek())) {
            if (detail::is_lower(peek())) has_lower = true;
            advance();
        }
        const std::size_t len = pos_ - start;

        std::string_view word;
        if (!has_lower) {
            word = source_.substr(start, len);
        } else {
            const std::size_t arena_start = r.arena.size();
            r.arena.resize(arena_start + len);
            for (std::size_t i = 0; i < len; ++i)
                r.arena[arena_start + i] = detail::to_upper(source_[start + i]);
            word = std::string_view(r.arena.data() + arena_start, len);
        }

        emit(r, lookup_keyword(word), word, start_line, start_col);
    }

    std::string_view source_;
    std::size_t       length_;
    std::size_t       pos_  = 0;
    std::uint32_t      line_ = 1;
    std::uint32_t      col_  = 1;
};

// ---------------------------------------------------------------------------
// Lazy numeric conversion for INT_LIT / FLOAT_LIT tokens
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::int64_t as_int(const Token& t) noexcept {
    std::int64_t v = 0;
    std::from_chars(t.value.data(), t.value.data() + t.value.size(), v);
    return v;
}

[[nodiscard]] inline double as_double(const Token& t) noexcept {
    double v = 0.0;
    std::from_chars(t.value.data(), t.value.data() + t.value.size(), v);
    return v;
}

} // namespace xpln
