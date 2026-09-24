#pragma once

#include "ast_nodes.hpp"
#include "tokens.hpp"
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace xpln {

class ParserError : public std::exception {
public:
    ParserError(std::string message, Token token)
        : msg_(std::format("Parser error at line {}, col {}: {}", token.line, token.col, message))
        , token_(token) {}

    [[nodiscard]] const char* what() const noexcept override { return msg_.c_str(); }
    [[nodiscard]] Token token() const noexcept { return token_; }

private:
    std::string msg_;
    Token token_;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    ast::Program parse();

private:
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& peek(std::size_t offset = 1) const noexcept;
    Token advance() noexcept;
    bool check(TokenType type) const noexcept;

    template <typename... Args>
    bool match(Args... types) noexcept {
        return (... || ([&]() {
            if (check(types)) { advance(); return true; }
            return false;
        }()));
    }

    Token expect(TokenType type, std::string_view message = "");

    ast::ProcedureDecl parse_procedure();
    ast::StmtPtr parse_statement();
    ast::DeclareStmt parse_declare();
    std::vector<ast::StmtPtr> parse_single_or_block();
    ast::IfStmt parse_if();
    ast::StmtPtr parse_do();
    ast::SelectStmt parse_select();
    ast::CallStmt parse_call_stmt();
    ast::ReturnStmt parse_return();
    ast::PutStmt parse_put();
    ast::GetStmt parse_get();
    ast::AssignStmt parse_assignment();

    // Expression parsing
    ast::ExprPtr parse_expression();
    ast::ExprPtr parse_or();
    ast::ExprPtr parse_and();
    ast::ExprPtr parse_not();
    ast::ExprPtr parse_comparison();
    ast::ExprPtr parse_concat();
    ast::ExprPtr parse_term();
    ast::ExprPtr parse_factor();
    ast::ExprPtr parse_power();
    ast::ExprPtr parse_unary();
    ast::ExprPtr parse_primary();

    const std::vector<Token>& tokens_;
    std::size_t pos_ = 0;
};

} // namespace xpln
