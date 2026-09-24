#include "parser.hpp"
#include <charconv>

namespace xpln {

const Token& Parser::current() const noexcept {
    if (pos_ < tokens_.size()) return tokens_[pos_];
    return tokens_.back();
}

const Token& Parser::peek(std::size_t offset) const noexcept {
    std::size_t idx = pos_ + offset;
    if (idx < tokens_.size()) return tokens_[idx];
    return tokens_.back();
}

Token Parser::advance() noexcept {
    Token token = current();
    if (token.type != TokenType::Eof) {
        ++pos_;
    }
    return token;
}

bool Parser::check(TokenType type) const noexcept {
    return current().type == type;
}

Token Parser::expect(TokenType type, std::string_view message) {
    if (check(type)) return advance();
    Token curr = current();
    std::string msg = message.empty()
        ? std::format("Expected {}, got {} ('{}')", to_string(type), to_string(curr.type), curr.value)
        : std::string(message);
    throw ParserError(msg, curr);
}

ast::Program Parser::parse() {
    ast::Program program;
    while (!check(TokenType::Eof)) {
        program.procedures.push_back(parse_procedure());
    }
    return program;
}

ast::ProcedureDecl Parser::parse_procedure() {
    std::string name;
    if (check(TokenType::Ident) && peek().type == TokenType::Colon) {
        name = std::string(advance().value);
        advance(); // :
    } else if (check(TokenType::Ident) && (peek().type == TokenType::Procedure || peek().type == TokenType::Proc)) {
        name = std::string(advance().value);
    }

    if (!match(TokenType::Procedure, TokenType::Proc)) {
        throw ParserError("Expected PROCEDURE or PROC", current());
    }

    if (name.empty()) name = "ANON_PROC";

    std::vector<std::string> params;
    if (match(TokenType::LParen)) {
        if (!check(TokenType::RParen)) {
            while (true) {
                params.push_back(std::string(expect(TokenType::Ident, "Expected parameter name").value));
                if (!match(TokenType::Comma)) break;
            }
        }
        expect(TokenType::RParen, "Expected ')' after parameter list");
    }

    std::optional<std::string> returns_type;
    bool is_main = false;

    while (!check(TokenType::Semicolon) && !check(TokenType::Eof)) {
        if (match(TokenType::Returns)) {
            expect(TokenType::LParen, "Expected '(' after RETURNS");
            std::string ret_type;
            while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
                if (!ret_type.empty()) ret_type += " ";
                ret_type += advance().value;
            }
            expect(TokenType::RParen, "Expected ')' after return type");
            returns_type = ret_type;
        } else if (match(TokenType::Options)) {
            expect(TokenType::LParen, "Expected '(' after OPTIONS");
            auto opt = advance().value;
            if (opt == "MAIN" || opt == "main") is_main = true;
            expect(TokenType::RParen, "Expected ')' after OPTIONS argument");
        } else {
            advance();
        }
    }

    expect(TokenType::Semicolon, "Expected ';' after PROCEDURE declaration");

    std::vector<ast::StmtPtr> body;
    while (!check(TokenType::End) && !check(TokenType::Eof)) {
        if (auto stmt = parse_statement()) body.push_back(std::move(stmt));
    }

    expect(TokenType::End, "Expected END at end of procedure");
    if (check(TokenType::Ident)) advance();
    expect(TokenType::Semicolon, "Expected ';' after END");

    return ast::ProcedureDecl{
        .name = std::move(name),
        .params = std::move(params),
        .returns_type = std::move(returns_type),
        .is_main = is_main,
        .body = std::move(body)
    };
}

ast::StmtPtr Parser::parse_statement() {
    if (check(TokenType::Ident) && peek().type == TokenType::Colon) {
        if (peek(2).type == TokenType::Procedure || peek(2).type == TokenType::Proc) {
            return std::make_unique<ast::Stmt>(parse_procedure());
        }
        advance(); // label
        advance(); // :
    }

    Token curr = current();
    if (curr.type == TokenType::Procedure || curr.type == TokenType::Proc) {
        return std::make_unique<ast::Stmt>(parse_procedure());
    }
    if (curr.type == TokenType::Declare || curr.type == TokenType::Dcl) {
        return std::make_unique<ast::Stmt>(parse_declare());
    }
    if (curr.type == TokenType::If) {
        return std::make_unique<ast::Stmt>(parse_if());
    }
    if (curr.type == TokenType::Do) {
        return parse_do();
    }
    if (curr.type == TokenType::Select) {
        return std::make_unique<ast::Stmt>(parse_select());
    }
    if (curr.type == TokenType::Call) {
        return std::make_unique<ast::Stmt>(parse_call_stmt());
    }
    if (curr.type == TokenType::Return) {
        return std::make_unique<ast::Stmt>(parse_return());
    }
    if (curr.type == TokenType::Stop) {
        advance();
        expect(TokenType::Semicolon, "Expected ';' after STOP");
        return std::make_unique<ast::Stmt>(ast::StopStmt{});
    }
    if (curr.type == TokenType::Put) {
        return std::make_unique<ast::Stmt>(parse_put());
    }
    if (curr.type == TokenType::Get) {
        return std::make_unique<ast::Stmt>(parse_get());
    }

    return std::make_unique<ast::Stmt>(parse_assignment());
}

ast::DeclareStmt Parser::parse_declare() {
    advance(); // DCL / DECLARE
    std::vector<ast::VarDecl> decls;

    while (true) {
        std::vector<std::string> names;
        std::optional<std::vector<ast::ArrayBound>> bounds_list;

        // 1. Parse identifiers (factored or single)
        if (match(TokenType::LParen)) {
            while (true) {
                names.emplace_back(expect(TokenType::Ident, "Expected identifier in declaration").value);

                // Handle array bounds directly inside factored name list: e.g., DECLARE (MARKS(1:5)) ...
                if (match(TokenType::LParen)) {
                    std::vector<ast::ArrayBound> bounds;
                    while (true) {
                        int low = 1;
                        auto high_tok = advance();
                        int high = 0;
                        std::from_chars(high_tok.value.data(), high_tok.value.data() + high_tok.value.size(), high);
                        if (match(TokenType::Colon)) {
                            low = high;
                            auto real_high_tok = advance();
                            std::from_chars(real_high_tok.value.data(), real_high_tok.value.data() + real_high_tok.value.size(), high);
                        }
                        bounds.push_back({low, high});
                        if (!match(TokenType::Comma)) break;
                    }
                    expect(TokenType::RParen, "Expected ')' after array bounds");
                    bounds_list = bounds;
                }

                if (!match(TokenType::Comma)) break;
            }
            expect(TokenType::RParen, "Expected ')' after factored names");
        } else {
            names.emplace_back(expect(TokenType::Ident, "Expected identifier in declaration").value);
        }

        // 2. Handle unfactored array bounds after name list: e.g., DECLARE MARKS(1:5) ...
        if (!bounds_list && match(TokenType::LParen)) {
            std::vector<ast::ArrayBound> bounds;
            while (true) {
                int low = 1;
                auto high_tok = advance();
                int high = 0;
                std::from_chars(high_tok.value.data(), high_tok.value.data() + high_tok.value.size(), high);
                if (match(TokenType::Colon)) {
                    low = high;
                    auto real_high_tok = advance();
                    std::from_chars(real_high_tok.value.data(), real_high_tok.value.data() + real_high_tok.value.size(), high);
                }
                bounds.push_back({low, high});
                if (!match(TokenType::Comma)) break;
            }
            expect(TokenType::RParen, "Expected ')' after array bounds");
            bounds_list = bounds;
        }

        // 3. Parse data types, attributes, and initializers
        std::string data_type = "FIXED BIN";
        std::optional<int> length;
        bool is_varying = false;
        ast::ExprPtr init_val = nullptr;

        while (!check(TokenType::Comma) && !check(TokenType::Semicolon) && !check(TokenType::Eof)) {
            if (match(TokenType::Fixed)) {
                if (match(TokenType::Binary, TokenType::Bin)) data_type = "FIXED BIN";
                else if (match(TokenType::Decimal, TokenType::Dec)) data_type = "FIXED DEC";
                else data_type = "FIXED BIN";
                if (match(TokenType::LParen)) { while (!match(TokenType::RParen)) advance(); }
            } else if (match(TokenType::Binary, TokenType::Bin)) {
                data_type = "FIXED BIN";
                if (match(TokenType::LParen)) { while (!match(TokenType::RParen)) advance(); }
            } else if (match(TokenType::Float)) {
                data_type = "FLOAT";
                if (match(TokenType::LParen)) { while (!match(TokenType::RParen)) advance(); }
            } else if (match(TokenType::Character, TokenType::Char)) {
                data_type = "CHAR";
                if (match(TokenType::LParen)) {
                    auto len_expr = parse_expression();
                    if (auto* lit = std::get_if<ast::LiteralExpr>(&len_expr->data)) {
                        int val = 0;
                        std::from_chars(lit->value.data(), lit->value.data() + lit->value.size(), val);
                        length = val;
                    }
                    expect(TokenType::RParen, "Expected ')' after CHAR length");
                }
            } else if (match(TokenType::Varying, TokenType::Var)) {
                is_varying = true;
            } else if (match(TokenType::Bit)) {
                data_type = "BIT";
                if (match(TokenType::LParen)) {
                    auto len_expr = parse_expression();
                    if (auto* lit = std::get_if<ast::LiteralExpr>(&len_expr->data)) {
                        int val = 0;
                        std::from_chars(lit->value.data(), lit->value.data() + lit->value.size(), val);
                        length = val;
                    }
                    expect(TokenType::RParen, "Expected ')' after BIT length");
                }
            } else if (match(TokenType::Init, TokenType::Initial)) {
                expect(TokenType::LParen, "Expected '(' after INIT");
                init_val = parse_expression();
                expect(TokenType::RParen, "Expected ')' after INIT value");
            } else {
                advance();
            }
        }

        // 4. Register variables to declaration list
        for (const auto& name : names) {
            decls.push_back(ast::VarDecl{
                .name = name,
                .data_type = data_type,
                .length = length,
                .is_varying = is_varying,
                .bounds = bounds_list,
                .init = ast::clone_expr(init_val)
            });
        }

        if (!match(TokenType::Comma)) break;
    }

    expect(TokenType::Semicolon, "Expected ';' at end of DECLARE statement");
    return ast::DeclareStmt{.decls = std::move(decls)};
}

std::vector<ast::StmtPtr> Parser::parse_single_or_block() {
    if (check(TokenType::Do)) {
        std::vector<ast::StmtPtr> res;
        res.push_back(parse_do());
        return res;
    }
    auto stmt = parse_statement();
    std::vector<ast::StmtPtr> res;
    if (stmt) res.push_back(std::move(stmt));
    return res;
}

ast::IfStmt Parser::parse_if() {
    expect(TokenType::If);
    auto cond = parse_expression();
    expect(TokenType::Then, "Expected THEN after IF condition");
    auto then_b = parse_single_or_block();
    std::vector<ast::StmtPtr> else_b;
    if (match(TokenType::Else)) {
        else_b = parse_single_or_block();
    }
    return ast::IfStmt{
        .condition = std::move(cond),
        .then_branch = std::move(then_b),
        .else_branch = std::move(else_b)
    };
}

ast::StmtPtr Parser::parse_do() {
    expect(TokenType::Do);

    // 1. Simple DO;
    if (match(TokenType::Semicolon)) {
        std::vector<ast::StmtPtr> body;
        while (!check(TokenType::End) && !check(TokenType::Eof)) {
            if (auto s = parse_statement()) body.push_back(std::move(s));
        }
        expect(TokenType::End, "Expected END for DO group");
        if (check(TokenType::Ident)) advance();
        expect(TokenType::Semicolon, "Expected ';' after END");
        return std::make_unique<ast::Stmt>(ast::DoGroupStmt{.body = std::move(body)});
    }

    // 2. DO WHILE (cond);
    if (match(TokenType::While)) {
        expect(TokenType::LParen, "Expected '(' after WHILE");
        auto cond = parse_expression();
        expect(TokenType::RParen, "Expected ')' after WHILE condition");
        expect(TokenType::Semicolon, "Expected ';' after DO WHILE");
        std::vector<ast::StmtPtr> body;
        while (!check(TokenType::End) && !check(TokenType::Eof)) {
            if (auto s = parse_statement()) body.push_back(std::move(s));
        }
        expect(TokenType::End, "Expected END for DO WHILE");
        if (check(TokenType::Ident)) advance();
        expect(TokenType::Semicolon, "Expected ';' after END");
        return std::make_unique<ast::Stmt>(ast::DoWhileStmt{.condition = std::move(cond), .body = std::move(body)});
    }

    // 3. DO UNTIL (cond);
    if (match(TokenType::Until)) {
        expect(TokenType::LParen, "Expected '(' after UNTIL");
        auto cond = parse_expression();
        expect(TokenType::RParen, "Expected ')' after UNTIL condition");
        expect(TokenType::Semicolon, "Expected ';' after DO UNTIL");
        std::vector<ast::StmtPtr> body;
        while (!check(TokenType::End) && !check(TokenType::Eof)) {
            if (auto s = parse_statement()) body.push_back(std::move(s));
        }
        expect(TokenType::End, "Expected END for DO UNTIL");
        if (check(TokenType::Ident)) advance();
        expect(TokenType::Semicolon, "Expected ';' after END");
        return std::make_unique<ast::Stmt>(ast::DoUntilStmt{.condition = std::move(cond), .body = std::move(body)});
    }

    // 4. Iterative DO
    std::string var_name(expect(TokenType::Ident, "Expected loop index variable").value);
    expect(TokenType::Assign, "Expected '=' in iterative DO");
    auto start_expr = parse_expression();
    expect(TokenType::To, "Expected TO in iterative DO");
    auto to_expr = parse_expression();

    ast::ExprPtr by_expr = nullptr;
    if (match(TokenType::By)) by_expr = parse_expression();

    ast::ExprPtr while_cond = nullptr;
    if (match(TokenType::While)) {
        expect(TokenType::LParen, "Expected '(' after WHILE");
        while_cond = parse_expression();
        expect(TokenType::RParen, "Expected ')' after WHILE condition");
    }

    ast::ExprPtr until_cond = nullptr;
    if (match(TokenType::Until)) {
        expect(TokenType::LParen, "Expected '(' after UNTIL");
        until_cond = parse_expression();
        expect(TokenType::RParen, "Expected ')' after UNTIL condition");
    }

    expect(TokenType::Semicolon, "Expected ';' after DO specification");
    std::vector<ast::StmtPtr> body;
    while (!check(TokenType::End) && !check(TokenType::Eof)) {
        if (auto s = parse_statement()) body.push_back(std::move(s));
    }
    expect(TokenType::End, "Expected END for iterative DO");
    if (check(TokenType::Ident)) advance();
    expect(TokenType::Semicolon, "Expected ';' after END");

    return std::make_unique<ast::Stmt>(ast::DoIterativeStmt{
        .var_name = std::move(var_name),
        .start = std::move(start_expr),
        .to = std::move(to_expr),
        .by = std::move(by_expr),
        .while_cond = std::move(while_cond),
        .until_cond = std::move(until_cond),
        .body = std::move(body)
    });
}

ast::SelectStmt Parser::parse_select() {
    expect(TokenType::Select);
    ast::ExprPtr select_expr = nullptr;
    if (!check(TokenType::Semicolon)) {
        if (match(TokenType::LParen)) {
            select_expr = parse_expression();
            expect(TokenType::RParen, "Expected ')' after SELECT expression");
        } else {
            select_expr = parse_expression();
        }
    }
    expect(TokenType::Semicolon, "Expected ';' after SELECT");

    std::vector<std::pair<std::vector<ast::ExprPtr>, std::vector<ast::StmtPtr>>> when_clauses;
    std::vector<ast::StmtPtr> otherwise_branch;

    while (!check(TokenType::End) && !check(TokenType::Eof)) {
        if (match(TokenType::When)) {
            expect(TokenType::LParen, "Expected '(' after WHEN");
            std::vector<ast::ExprPtr> conds;
            while (true) {
                conds.push_back(parse_expression());
                if (!match(TokenType::Comma)) break;
            }
            expect(TokenType::RParen, "Expected ')' after WHEN conditions");
            auto stmts = parse_single_or_block();
            when_clauses.emplace_back(std::move(conds), std::move(stmts));
        } else if (match(TokenType::Otherwise, TokenType::Other)) {
            otherwise_branch = parse_single_or_block();
        } else {
            break;
        }
    }

    expect(TokenType::End, "Expected END for SELECT");
    if (check(TokenType::Ident)) advance();
    expect(TokenType::Semicolon, "Expected ';' after END");

    return ast::SelectStmt{
        .expr = std::move(select_expr),
        .when_clauses = std::move(when_clauses),
        .otherwise_branch = std::move(otherwise_branch)
    };
}

ast::CallStmt Parser::parse_call_stmt() {
    expect(TokenType::Call);
    std::string name(expect(TokenType::Ident, "Expected procedure name after CALL").value);
    std::vector<ast::ExprPtr> args;
    if (match(TokenType::LParen)) {
        if (!check(TokenType::RParen)) {
            while (true) {
                args.push_back(parse_expression());
                if (!match(TokenType::Comma)) break;
            }
        }
        expect(TokenType::RParen, "Expected ')' after CALL arguments");
    }
    expect(TokenType::Semicolon, "Expected ';' after CALL statement");
    return ast::CallStmt{.callee = std::move(name), .args = std::move(args)};
}

ast::ReturnStmt Parser::parse_return() {
    expect(TokenType::Return);
    ast::ExprPtr val = nullptr;
    if (!check(TokenType::Semicolon)) {
        if (match(TokenType::LParen)) {
            val = parse_expression();
            expect(TokenType::RParen, "Expected ')' after RETURN value");
        } else {
            val = parse_expression();
        }
    }
    expect(TokenType::Semicolon, "Expected ';' after RETURN");
    return ast::ReturnStmt{.value = std::move(val)};
}

ast::PutStmt Parser::parse_put() {
    expect(TokenType::Put);
    bool skip = false;
    ast::ExprPtr skip_count = nullptr;
    std::vector<ast::ExprPtr> items;
    std::optional<std::vector<std::string>> formats;

    while (!check(TokenType::Semicolon) && !check(TokenType::Eof)) {
        if (match(TokenType::Skip)) {
            skip = true;
            if (match(TokenType::LParen)) {
                skip_count = parse_expression();
                expect(TokenType::RParen, "Expected ')' after SKIP count");
            }
        } else if (match(TokenType::Line, TokenType::Page)) {
            skip = true;
            if (match(TokenType::LParen)) {
                parse_expression();
                expect(TokenType::RParen);
            }
        } else if (match(TokenType::List)) {
            expect(TokenType::LParen, "Expected '(' after LIST");
            if (!check(TokenType::RParen)) {
                while (true) {
                    items.push_back(parse_expression());
                    if (!match(TokenType::Comma)) break;
                }
            }
            expect(TokenType::RParen, "Expected ')' after LIST items");
        } else if (match(TokenType::Edit)) {
            expect(TokenType::LParen, "Expected '(' after EDIT");
            while (true) {
                items.push_back(parse_expression());
                if (!match(TokenType::Comma)) break;
            }
            expect(TokenType::RParen, "Expected ')' after EDIT items");
            expect(TokenType::LParen, "Expected '(' for format specifications");
            std::string fmt_str;
            int paren_depth = 1;
            while (paren_depth > 0 && !check(TokenType::Eof)) {
                auto t = advance();
                if (t.type == TokenType::LParen) ++paren_depth;
                else if (t.type == TokenType::RParen) {
                    --paren_depth;
                    if (paren_depth == 0) break;
                }
                if (!fmt_str.empty()) fmt_str += " ";
                fmt_str += t.value;
            }
            formats = std::vector<std::string>{fmt_str};
        } else {
            if (match(TokenType::LParen)) {
                while (true) {
                    items.push_back(parse_expression());
                    if (!match(TokenType::Comma)) break;
                }
                expect(TokenType::RParen);
            } else {
                advance();
            }
        }
    }

    expect(TokenType::Semicolon, "Expected ';' after PUT statement");
    return ast::PutStmt{
        .skip = skip,
        .skip_count = std::move(skip_count),
        .items = std::move(items),
        .formats = std::move(formats)
    };
}

ast::GetStmt Parser::parse_get() {
    expect(TokenType::Get);
    if (match(TokenType::Skip)) {
        if (match(TokenType::LParen)) {
            parse_expression();
            expect(TokenType::RParen);
        }
    }
    expect(TokenType::List, "Expected LIST in GET statement");
    expect(TokenType::LParen, "Expected '(' after LIST");
    std::vector<ast::ExprPtr> items;
    while (true) {
        items.push_back(parse_primary());
        if (!match(TokenType::Comma)) break;
    }
    expect(TokenType::RParen, "Expected ')' after GET targets");
    expect(TokenType::Semicolon, "Expected ';' after GET statement");
    return ast::GetStmt{.items = std::move(items)};
}

ast::AssignStmt Parser::parse_assignment() {
    if (check(TokenType::Ident) && current().value == "SUBSTR" && peek().type == TokenType::LParen) {
        advance();
        expect(TokenType::LParen);
        auto target = parse_expression();
        expect(TokenType::Comma);
        auto start = parse_expression();
        ast::ExprPtr length = nullptr;
        if (match(TokenType::Comma)) length = parse_expression();
        expect(TokenType::RParen);
        auto substr_pseudo = std::make_unique<ast::Expr>(ast::SubstrExpr{
            .target = std::move(target),
            .start = std::move(start),
            .length = std::move(length)
        });
        expect(TokenType::Assign, "Expected '=' in SUBSTR assignment");
        auto val = parse_expression();
        expect(TokenType::Semicolon, "Expected ';' after assignment");
        return ast::AssignStmt{.target = std::move(substr_pseudo), .expr = std::move(val)};
    }

    std::string target_name(expect(TokenType::Ident, "Expected identifier in declaration or statement").value);
    ast::ExprPtr target;
    if (match(TokenType::LParen)) {
        std::vector<ast::ExprPtr> args;
        while (true) {
            args.push_back(parse_expression());
            if (!match(TokenType::Comma)) break;
        }
        expect(TokenType::RParen, "Expected ')' after array indices");
        target = std::make_unique<ast::Expr>(ast::CallOrIndexExpr{
            .callee = target_name,
            .args = std::move(args)
        });
    } else {
        target = std::make_unique<ast::Expr>(ast::VarExpr{.name = target_name});
    }

    expect(TokenType::Assign, std::format("Expected '=' after assignment target '{}'", target_name));
    auto val = parse_expression();
    expect(TokenType::Semicolon, "Expected ';' after assignment");
    return ast::AssignStmt{.target = std::move(target), .expr = std::move(val)};
}

ast::ExprPtr Parser::parse_expression() { return parse_or(); }

ast::ExprPtr Parser::parse_or() {
    auto expr = parse_and();
    while (match(TokenType::Or)) {
        auto right = parse_and();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = "|",
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_and() {
    auto expr = parse_not();
    while (match(TokenType::And)) {
        auto right = parse_not();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = "&",
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_not() {
    if (match(TokenType::Not)) {
        auto operand = parse_not();
        return std::make_unique<ast::Expr>(ast::UnaryExpr{.op = "^", .expr = std::move(operand)});
    }
    return parse_comparison();
}

ast::ExprPtr Parser::parse_comparison() {
    auto expr = parse_concat();
    while (current().type == TokenType::Assign || current().type == TokenType::Eq ||
           current().type == TokenType::Ne || current().type == TokenType::Lt ||
           current().type == TokenType::Le || current().type == TokenType::Gt ||
           current().type == TokenType::Ge) {
        auto tok = advance();
        std::string op(tok.value);
        auto right = parse_concat();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = std::move(op),
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_concat() {
    auto expr = parse_term();
    while (match(TokenType::Concat)) {
        auto right = parse_term();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = "||",
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_term() {
    auto expr = parse_factor();
    while (current().type == TokenType::Plus || current().type == TokenType::Minus) {
        std::string op(advance().value);
        auto right = parse_factor();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = std::move(op),
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_factor() {
    auto expr = parse_power();
    while (current().type == TokenType::Star || current().type == TokenType::Slash) {
        std::string op(advance().value);
        auto right = parse_power();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = std::move(op),
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_power() {
    auto expr = parse_unary();
    if (match(TokenType::Power)) {
        auto right = parse_power();
        expr = std::make_unique<ast::Expr>(ast::BinaryExpr{
            .op = "**",
            .left = std::move(expr),
            .right = std::move(right)
        });
    }
    return expr;
}

ast::ExprPtr Parser::parse_unary() {
    if (current().type == TokenType::Plus || current().type == TokenType::Minus) {
        std::string op(advance().value);
        auto operand = parse_unary();
        return std::make_unique<ast::Expr>(ast::UnaryExpr{.op = std::move(op), .expr = std::move(operand)});
    }
    return parse_primary();
}

ast::ExprPtr Parser::parse_primary() {
    Token curr = current();

    if (match(TokenType::IntLit)) {
        return std::make_unique<ast::Expr>(ast::LiteralExpr{.value = std::string(curr.value), .lit_type = ast::LiteralExpr::Type::Int});
    }
    if (match(TokenType::FloatLit)) {
        return std::make_unique<ast::Expr>(ast::LiteralExpr{.value = std::string(curr.value), .lit_type = ast::LiteralExpr::Type::Float});
    }
    if (match(TokenType::StringLit)) {
        return std::make_unique<ast::Expr>(ast::LiteralExpr{.value = std::string(curr.value), .lit_type = ast::LiteralExpr::Type::String});
    }
    if (match(TokenType::BitLit)) {
        return std::make_unique<ast::Expr>(ast::LiteralExpr{.value = std::string(curr.value), .lit_type = ast::LiteralExpr::Type::Bit});
    }
    if (match(TokenType::LParen)) {
        auto expr = parse_expression();
        expect(TokenType::RParen, "Expected ')' after expression");
        return expr;
    }
    if (match(TokenType::Ident)) {
        std::string name(curr.value);
        if (match(TokenType::LParen)) {
            if (name == "SUBSTR") {
                auto target = parse_expression();
                expect(TokenType::Comma, "Expected ',' in SUBSTR");
                auto start = parse_expression();
                ast::ExprPtr length = nullptr;
                if (match(TokenType::Comma)) length = parse_expression();
                expect(TokenType::RParen, "Expected ')' after SUBSTR");
                return std::make_unique<ast::Expr>(ast::SubstrExpr{
                    .target = std::move(target),
                    .start = std::move(start),
                    .length = std::move(length)
                });
            }

            std::vector<ast::ExprPtr> args;
            if (!check(TokenType::RParen)) {
                while (true) {
                    args.push_back(parse_expression());
                    if (!match(TokenType::Comma)) break;
                }
            }
            expect(TokenType::RParen, "Expected ')' after arguments/subscripts");
            return std::make_unique<ast::Expr>(ast::CallOrIndexExpr{.callee = std::move(name), .args = std::move(args)});
        }
        return std::make_unique<ast::Expr>(ast::VarExpr{.name = std::move(name)});
    }

    throw ParserError(std::format("Unexpected token in expression: {} ('{}')", to_string(curr.type), curr.value), curr);
}

} // namespace xpln
