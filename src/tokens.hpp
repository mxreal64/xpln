#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <format>
#include <ostream>

namespace xpln {

// ---------------------------------------------------------------------------
// TokenType
// ---------------------------------------------------------------------------

enum class TokenType : std::uint8_t {
    // End of file
    Eof,

    // Identifiers and literals
    Ident, IntLit, FloatLit, StringLit, BitLit,

    // Reserved keywords
    Procedure, Proc, End, Declare, Dcl, Returns, Options, Main,
    If, Then, Else, Do, To, By, While, Until,
    Select, When, Otherwise, Other,
    Call, Return, Goto, Go, Stop,
    Put, Get, Skip, List, Edit, Page, Line,
    Init, Initial,

    // Types and attributes
    Fixed, Binary, Bin, Decimal, Dec, Float,
    Character, Char, Varying, Var, Bit, Entry,

    // Operators
    Plus,    // +
    Minus,   // -
    Star,    // *
    Slash,   // /
    Power,   // **
    Concat,  // ||
    Assign,  // =
    Eq,      // = (expression context)
    Ne,      // ^=, \=, !=, <>
    Lt,      // <
    Le,      // <=
    Gt,      // >
    Ge,      // >=
    And,     // &
    Or,      // |
    Not,     // ^, \, ~, !

    // Delimiters
    Semicolon, Colon, Comma, LParen, RParen,

    Count_  // sentinel: number of enumerators, not a real token
};

inline constexpr std::size_t kTokenTypeCount = static_cast<std::size_t>(TokenType::Count_);

// Human-readable name table, indexed directly by the enum's underlying value.
// A single flat array load — no switch, no branching.
inline constexpr std::array<std::string_view, kTokenTypeCount> kTokenTypeNames = {
    "EOF",
    "IDENT", "INT_LIT", "FLOAT_LIT", "STRING_LIT", "BIT_LIT",
    "PROCEDURE", "PROC", "END", "DECLARE", "DCL", "RETURNS", "OPTIONS", "MAIN",
    "IF", "THEN", "ELSE", "DO", "TO", "BY", "WHILE", "UNTIL",
    "SELECT", "WHEN", "OTHERWISE", "OTHER",
    "CALL", "RETURN", "GOTO", "GO", "STOP",
    "PUT", "GET", "SKIP", "LIST", "EDIT", "PAGE", "LINE",
    "INIT", "INITIAL",
    "FIXED", "BINARY", "BIN", "DECIMAL", "DEC", "FLOAT",
    "CHARACTER", "CHAR", "VARYING", "VAR", "BIT", "ENTRY",
    "PLUS", "MINUS", "STAR", "SLASH", "POWER", "CONCAT",
    "ASSIGN", "EQ", "NE", "LT", "LE", "GT", "GE", "AND", "OR", "NOT",
    "SEMICOLON", "COLON", "COMMA", "LPAREN", "RPAREN",
};

[[nodiscard]] constexpr std::string_view to_string(TokenType t) noexcept {
    return kTokenTypeNames[static_cast<std::size_t>(t)];
}

// ---------------------------------------------------------------------------
// Compile-time FNV-1a hashing, used to build a perfect-hash switch for the
// keyword table below.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view s) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

namespace detail {

// One entry per keyword. Kept as a constexpr array purely so a
// static_assert can prove — at compile time — that every hash below is
// unique, which is what makes it safe to skip a runtime string
// re-comparison inside lookup_keyword().
inline constexpr std::array<std::pair<std::string_view, TokenType>, 46> kKeywordTable{{
    {"PROCEDURE", TokenType::Procedure}, {"PROC", TokenType::Proc},
    {"END", TokenType::End},             {"DECLARE", TokenType::Declare},
    {"DCL", TokenType::Dcl},             {"RETURNS", TokenType::Returns},
    {"OPTIONS", TokenType::Options},     {"MAIN", TokenType::Main},
    {"IF", TokenType::If},               {"THEN", TokenType::Then},
    {"ELSE", TokenType::Else},           {"DO", TokenType::Do},
    {"TO", TokenType::To},               {"BY", TokenType::By},
    {"WHILE", TokenType::While},         {"UNTIL", TokenType::Until},
    {"SELECT", TokenType::Select},       {"WHEN", TokenType::When},
    {"OTHERWISE", TokenType::Otherwise}, {"OTHER", TokenType::Other},
    {"CALL", TokenType::Call},           {"RETURN", TokenType::Return},
    {"GOTO", TokenType::Goto},           {"GO", TokenType::Go},
    {"STOP", TokenType::Stop},           {"PUT", TokenType::Put},
    {"GET", TokenType::Get},             {"SKIP", TokenType::Skip},
    {"LIST", TokenType::List},           {"EDIT", TokenType::Edit},
    {"PAGE", TokenType::Page},           {"LINE", TokenType::Line},
    {"INIT", TokenType::Init},           {"INITIAL", TokenType::Initial},
    {"FIXED", TokenType::Fixed},         {"BINARY", TokenType::Binary},
    {"BIN", TokenType::Bin},             {"DECIMAL", TokenType::Decimal},
    {"DEC", TokenType::Dec},             {"FLOAT", TokenType::Float},
    {"CHARACTER", TokenType::Character}, {"CHAR", TokenType::Char},
    {"VARYING", TokenType::Varying},     {"VAR", TokenType::Var},
    {"BIT", TokenType::Bit},             {"ENTRY", TokenType::Entry},
}};

[[nodiscard]] consteval bool hashes_are_collision_free() noexcept {
    for (std::size_t i = 0; i < kKeywordTable.size(); ++i)
        for (std::size_t j = i + 1; j < kKeywordTable.size(); ++j)
            if (fnv1a(kKeywordTable[i].first) == fnv1a(kKeywordTable[j].first))
                return false;
    return true;
}

static_assert(hashes_are_collision_free(),
              "FNV-1a collision among PL/I keywords: widen the hash or add a fallback compare");

} // namespace detail

// Classifies `word` as a keyword TokenType, or TokenType::Ident if it isn't
// one. `word` should already be upper-cased by the caller (matching the
// Python lexer's behavior of keying KEYWORDS by upper-case strings).
//
// Compiles to a single hash computation followed by a jump table / compare
// tree over 46 compile-time-known constants — no allocation, no dynamic
// dictionary, and (for a literal argument) is foldable entirely at compile
// time.
[[nodiscard]] constexpr TokenType lookup_keyword(std::string_view word) noexcept {
    switch (fnv1a(word)) {
        case fnv1a("PROCEDURE"): return TokenType::Procedure;
        case fnv1a("PROC"):      return TokenType::Proc;
        case fnv1a("END"):       return TokenType::End;
        case fnv1a("DECLARE"):   return TokenType::Declare;
        case fnv1a("DCL"):       return TokenType::Dcl;
        case fnv1a("RETURNS"):   return TokenType::Returns;
        case fnv1a("OPTIONS"):   return TokenType::Options;
        case fnv1a("MAIN"):      return TokenType::Main;
        case fnv1a("IF"):        return TokenType::If;
        case fnv1a("THEN"):      return TokenType::Then;
        case fnv1a("ELSE"):      return TokenType::Else;
        case fnv1a("DO"):        return TokenType::Do;
        case fnv1a("TO"):        return TokenType::To;
        case fnv1a("BY"):        return TokenType::By;
        case fnv1a("WHILE"):     return TokenType::While;
        case fnv1a("UNTIL"):     return TokenType::Until;
        case fnv1a("SELECT"):    return TokenType::Select;
        case fnv1a("WHEN"):      return TokenType::When;
        case fnv1a("OTHERWISE"): return TokenType::Otherwise;
        case fnv1a("OTHER"):     return TokenType::Other;
        case fnv1a("CALL"):      return TokenType::Call;
        case fnv1a("RETURN"):    return TokenType::Return;
        case fnv1a("GOTO"):      return TokenType::Goto;
        case fnv1a("GO"):        return TokenType::Go;
        case fnv1a("STOP"):      return TokenType::Stop;
        case fnv1a("PUT"):       return TokenType::Put;
        case fnv1a("GET"):       return TokenType::Get;
        case fnv1a("SKIP"):      return TokenType::Skip;
        case fnv1a("LIST"):      return TokenType::List;
        case fnv1a("EDIT"):      return TokenType::Edit;
        case fnv1a("PAGE"):      return TokenType::Page;
        case fnv1a("LINE"):      return TokenType::Line;
        case fnv1a("INIT"):      return TokenType::Init;
        case fnv1a("INITIAL"):   return TokenType::Initial;
        case fnv1a("FIXED"):     return TokenType::Fixed;
        case fnv1a("BINARY"):    return TokenType::Binary;
        case fnv1a("BIN"):       return TokenType::Bin;
        case fnv1a("DECIMAL"):   return TokenType::Decimal;
        case fnv1a("DEC"):       return TokenType::Dec;
        case fnv1a("FLOAT"):     return TokenType::Float;
        case fnv1a("CHARACTER"): return TokenType::Character;
        case fnv1a("CHAR"):      return TokenType::Char;
        case fnv1a("VARYING"):   return TokenType::Varying;
        case fnv1a("VAR"):       return TokenType::Var;
        case fnv1a("BIT"):       return TokenType::Bit;
        case fnv1a("ENTRY"):     return TokenType::Entry;
        default:                 return TokenType::Ident;
    }
}

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------

// Deliberately trivially-copyable and small (24 bytes on a 64-bit target:
// 16-byte string_view + 4-byte line + 4-byte col). No vtable, no heap, no
// reference counting — unlike the Python `Token` object, which carries
// per-instance `__dict__` overhead.
//
// `value` is a non-owning view: it must point into storage that outlives
// the Token (typically the source buffer the lexer scanned from), exactly
// like Python's `str` slices reference the original source text conceptually
// but without the allocation Python actually performs for each substring.
struct Token {
    TokenType        type;
    std::string_view value;
    std::uint32_t    line;
    std::uint32_t    col;

    constexpr Token(TokenType type_, std::string_view value_,
                     std::uint32_t line_, std::uint32_t col_) noexcept
        : type(type_), value(value_), line(line_), col(col_) {}

    [[nodiscard]] friend constexpr bool operator==(const Token&, const Token&) noexcept = default;
};

} // namespace xpln

// ---------------------------------------------------------------------------
// std::format / std::ostream support — mirrors Python's Token.__repr__:
//   Token(TYPE, 'value', line=L, col=C)
// ---------------------------------------------------------------------------

template <>
struct std::formatter<xpln::Token> {
    static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    static auto format(const xpln::Token& t, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Token({}, '{}', line={}, col={})",
                               xpln::to_string(t.type), t.value, t.line, t.col);
    }
};

inline std::ostream& operator<<(std::ostream& os, const xpln::Token& t) {
    return os << std::format("{}", t);
}
