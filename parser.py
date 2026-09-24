from typing import Optional, List
from tokens import TokenType, Token
import ast_nodes as ast

class ParserError(Exception):
    def __init__(self, message: str, token: Token):
        super().__init__(f"Parser error at line {token.line}, col {token.col}: {message}")
        self.token = token

class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def current(self) -> Token:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return self.tokens[-1]

    def peek(self, offset: int = 1) -> Token:
        idx = self.pos + offset
        if idx < len(self.tokens):
            return self.tokens[idx]
        return self.tokens[-1]

    def advance(self) -> Token:
        token = self.current()
        if token.type != TokenType.EOF:
            self.pos += 1
        return token

    def check(self, type_: TokenType) -> bool:
        return self.current().type == type_

    def match(self, *types: TokenType) -> bool:
        for t in types:
            if self.check(t):
                self.advance()
                return True
        return False

    def expect(self, type_: TokenType, message: str = "") -> Token:
        if self.check(type_):
            return self.advance()
        curr = self.current()
        msg = message or f"Expected {type_.name}, got {curr.type.name} ('{curr.value}')"
        raise ParserError(msg, curr)

    def parse(self) -> ast.Program:
        procedures = []
        while not self.check(TokenType.EOF):
            proc = self.parse_procedure()
            procedures.append(proc)
        return ast.Program(procedures=procedures)

    def parse_procedure(self) -> ast.ProcedureDecl:
        name = ""
        if self.check(TokenType.IDENT) and self.peek().type == TokenType.COLON:
            name = self.advance().value
            self.advance()  # :
        elif self.check(TokenType.IDENT) and (self.peek().type in (TokenType.PROCEDURE, TokenType.PROC)):
            name = self.advance().value

        if not self.match(TokenType.PROCEDURE, TokenType.PROC):
            raise ParserError("Expected PROCEDURE or PROC", self.current())

        if not name:
            name = "ANON_PROC"

        # Parameters
        params = []
        if self.match(TokenType.LPAREN):
            if not self.check(TokenType.RPAREN):
                while True:
                    param = self.expect(TokenType.IDENT, "Expected parameter name").value
                    params.append(param)
                    if not self.match(TokenType.COMMA):
                        break
            self.expect(TokenType.RPAREN, "Expected ')' after parameter list")

        returns_type = None
        is_main = False

        while not self.check(TokenType.SEMICOLON) and not self.check(TokenType.EOF):
            if self.match(TokenType.RETURNS):
                self.expect(TokenType.LPAREN, "Expected '(' after RETURNS")
                ret_type_tokens = []
                while not self.check(TokenType.RPAREN) and not self.check(TokenType.EOF):
                    ret_type_tokens.append(self.advance().value)
                self.expect(TokenType.RPAREN, "Expected ')' after return type")
                returns_type = " ".join(str(x) for x in ret_type_tokens)
            elif self.match(TokenType.OPTIONS):
                self.expect(TokenType.LPAREN, "Expected '(' after OPTIONS")
                opt = self.advance().value
                if str(opt).upper() == "MAIN":
                    is_main = True
                self.expect(TokenType.RPAREN, "Expected ')' after OPTIONS argument")
            else:
                self.advance()

        self.expect(TokenType.SEMICOLON, "Expected ';' after PROCEDURE declaration")

        # Body
        body = []
        while not self.check(TokenType.END) and not self.check(TokenType.EOF):
            stmt = self.parse_statement()
            if stmt:
                body.append(stmt)

        self.expect(TokenType.END, "Expected END at end of procedure")
        if self.check(TokenType.IDENT):
            self.advance()
        self.expect(TokenType.SEMICOLON, "Expected ';' after END")

        return ast.ProcedureDecl(
            name=name,
            params=params,
            returns_type=returns_type,
            is_main=is_main,
            body=body
        )

    def parse_statement(self) -> Optional[ast.Stmt]:
        if self.check(TokenType.IDENT) and self.peek().type == TokenType.COLON:
            if self.peek(2).type in (TokenType.PROCEDURE, TokenType.PROC):
                return self.parse_procedure()
            self.advance()  # label
            self.advance()  # :

        curr = self.current()

        if curr.type in (TokenType.PROCEDURE, TokenType.PROC):
            return self.parse_procedure()

        if curr.type in (TokenType.DECLARE, TokenType.DCL):
            return self.parse_declare()

        if curr.type == TokenType.IF:
            return self.parse_if()

        if curr.type == TokenType.DO:
            return self.parse_do()

        if curr.type == TokenType.SELECT:
            return self.parse_select()

        if curr.type == TokenType.CALL:
            return self.parse_call_stmt()

        if curr.type == TokenType.RETURN:
            return self.parse_return()

        if curr.type == TokenType.STOP:
            self.advance()
            self.expect(TokenType.SEMICOLON, "Expected ';' after STOP")
            return ast.StopStmt()

        if curr.type == TokenType.PUT:
            return self.parse_put()

        if curr.type == TokenType.GET:
            return self.parse_get()

        return self.parse_assignment()

    def parse_declare(self) -> ast.DeclareStmt:
        self.advance()  # DCL / DECLARE
        decls = []

        while True:
            names = []
            bounds_list = None
            if self.match(TokenType.LPAREN):
                while True:
                    n = self.expect(TokenType.IDENT, "Expected identifier in declaration").value
                    names.append(n)
                    if not self.match(TokenType.COMMA):
                        break
                self.expect(TokenType.RPAREN, "Expected ')' after factored names")
            else:
                n = self.expect(TokenType.IDENT, "Expected identifier in declaration").value
                names.append(n)

            if self.match(TokenType.LPAREN):
                bounds = []
                while True:
                    low = 1
                    high_tok = self.advance()
                    high = int(high_tok.value)
                    if self.match(TokenType.COLON):
                        low = high
                        high = int(self.advance().value)
                    bounds.append((low, high))
                    if not self.match(TokenType.COMMA):
                        break
                self.expect(TokenType.RPAREN, "Expected ')' after array bounds")
                bounds_list = bounds

            data_type = "FIXED BIN"
            length = None
            is_varying = False
            init_val = None

            while not self.check(TokenType.COMMA) and not self.check(TokenType.SEMICOLON) and not self.check(TokenType.EOF):
                if self.match(TokenType.FIXED):
                    if self.match(TokenType.BINARY, TokenType.BIN):
                        data_type = "FIXED BIN"
                    elif self.match(TokenType.DECIMAL, TokenType.DEC):
                        data_type = "FIXED DEC"
                    else:
                        data_type = "FIXED BIN"
                    if self.match(TokenType.LPAREN):
                        while not self.match(TokenType.RPAREN):
                            self.advance()
                elif self.match(TokenType.BINARY, TokenType.BIN):
                    data_type = "FIXED BIN"
                    if self.match(TokenType.LPAREN):
                        while not self.match(TokenType.RPAREN):
                            self.advance()
                elif self.match(TokenType.FLOAT):
                    data_type = "FLOAT"
                    if self.match(TokenType.LPAREN):
                        while not self.match(TokenType.RPAREN):
                            self.advance()
                elif self.match(TokenType.CHARACTER, TokenType.CHAR):
                    data_type = "CHAR"
                    if self.match(TokenType.LPAREN):
                        len_expr = self.parse_expression()
                        if isinstance(len_expr, ast.LiteralExpr):
                            length = int(len_expr.value)
                        self.expect(TokenType.RPAREN, "Expected ')' after CHAR length")
                elif self.match(TokenType.VARYING, TokenType.VAR):
                    is_varying = True
                elif self.match(TokenType.BIT):
                    data_type = "BIT"
                    if self.match(TokenType.LPAREN):
                        len_expr = self.parse_expression()
                        if isinstance(len_expr, ast.LiteralExpr):
                            length = int(len_expr.value)
                        self.expect(TokenType.RPAREN, "Expected ')' after BIT length")
                elif self.match(TokenType.INIT, TokenType.INITIAL):
                    self.expect(TokenType.LPAREN, "Expected '(' after INIT")
                    init_val = self.parse_expression()
                    self.expect(TokenType.RPAREN, "Expected ')' after INIT value")
                else:
                    self.advance()

            for name in names:
                decls.append(ast.VarDecl(
                    name=name,
                    data_type=data_type,
                    length=length,
                    is_varying=is_varying,
                    bounds=bounds_list,
                    init=init_val
                ))

            if not self.match(TokenType.COMMA):
                break

        self.expect(TokenType.SEMICOLON, "Expected ';' at end of DECLARE statement")
        return ast.DeclareStmt(decls=decls)

    def parse_single_or_block(self) -> List[ast.Stmt]:
        if self.check(TokenType.DO):
            return [self.parse_do()]
        else:
            stmt = self.parse_statement()
            return [stmt] if stmt else []

    def parse_if(self) -> ast.IfStmt:
        self.expect(TokenType.IF)
        condition = self.parse_expression()
        self.expect(TokenType.THEN, "Expected THEN after IF condition")
        then_branch = self.parse_single_or_block()
        else_branch = None
        if self.match(TokenType.ELSE):
            else_branch = self.parse_single_or_block()
        return ast.IfStmt(condition=condition, then_branch=then_branch, else_branch=else_branch)

    def parse_do(self) -> ast.Stmt:
        self.expect(TokenType.DO)

        # 1. Simple DO;
        if self.match(TokenType.SEMICOLON):
            body = []
            while not self.check(TokenType.END) and not self.check(TokenType.EOF):
                s = self.parse_statement()
                if s: body.append(s)
            self.expect(TokenType.END, "Expected END for DO group")
            if self.check(TokenType.IDENT): self.advance()
            self.expect(TokenType.SEMICOLON, "Expected ';' after END")
            return ast.DoGroupStmt(body=body)

        # 2. DO WHILE (cond);
        if self.match(TokenType.WHILE):
            self.expect(TokenType.LPAREN, "Expected '(' after WHILE")
            cond = self.parse_expression()
            self.expect(TokenType.RPAREN, "Expected ')' after WHILE condition")
            self.expect(TokenType.SEMICOLON, "Expected ';' after DO WHILE")
            body = []
            while not self.check(TokenType.END) and not self.check(TokenType.EOF):
                s = self.parse_statement()
                if s: body.append(s)
            self.expect(TokenType.END, "Expected END for DO WHILE")
            if self.check(TokenType.IDENT): self.advance()
            self.expect(TokenType.SEMICOLON, "Expected ';' after END")
            return ast.DoWhileStmt(condition=cond, body=body)

        # 3. DO UNTIL (cond);
        if self.match(TokenType.UNTIL):
            self.expect(TokenType.LPAREN, "Expected '(' after UNTIL")
            cond = self.parse_expression()
            self.expect(TokenType.RPAREN, "Expected ')' after UNTIL condition")
            self.expect(TokenType.SEMICOLON, "Expected ';' after DO UNTIL")
            body = []
            while not self.check(TokenType.END) and not self.check(TokenType.EOF):
                s = self.parse_statement()
                if s: body.append(s)
            self.expect(TokenType.END, "Expected END for DO UNTIL")
            if self.check(TokenType.IDENT): self.advance()
            self.expect(TokenType.SEMICOLON, "Expected ';' after END")
            return ast.DoUntilStmt(condition=cond, body=body)

        # 4. Iterative DO
        var_name = self.expect(TokenType.IDENT, "Expected loop index variable").value
        self.expect(TokenType.ASSIGN, "Expected '=' in iterative DO")
        start_expr = self.parse_expression()
        self.expect(TokenType.TO, "Expected TO in iterative DO")
        to_expr = self.parse_expression()

        by_expr = None
        if self.match(TokenType.BY):
            by_expr = self.parse_expression()

        while_cond = None
        if self.match(TokenType.WHILE):
            self.expect(TokenType.LPAREN, "Expected '(' after WHILE")
            while_cond = self.parse_expression()
            self.expect(TokenType.RPAREN, "Expected ')' after WHILE condition")

        until_cond = None
        if self.match(TokenType.UNTIL):
            self.expect(TokenType.LPAREN, "Expected '(' after UNTIL")
            until_cond = self.parse_expression()
            self.expect(TokenType.RPAREN, "Expected ')' after UNTIL condition")

        self.expect(TokenType.SEMICOLON, "Expected ';' after DO specification")
        body = []
        while not self.check(TokenType.END) and not self.check(TokenType.EOF):
            s = self.parse_statement()
            if s: body.append(s)
        self.expect(TokenType.END, "Expected END for iterative DO")
        if self.check(TokenType.IDENT): self.advance()
        self.expect(TokenType.SEMICOLON, "Expected ';' after END")

        return ast.DoIterativeStmt(
            var_name=var_name,
            start=start_expr,
            to=to_expr,
            by=by_expr,
            while_cond=while_cond,
            until_cond=until_cond,
            body=body
        )

    def parse_select(self) -> ast.SelectStmt:
        self.expect(TokenType.SELECT)
        select_expr = None
        if not self.check(TokenType.SEMICOLON):
            if self.match(TokenType.LPAREN):
                select_expr = self.parse_expression()
                self.expect(TokenType.RPAREN, "Expected ')' after SELECT expression")
            else:
                select_expr = self.parse_expression()
        self.expect(TokenType.SEMICOLON, "Expected ';' after SELECT")

        when_clauses = []
        otherwise_branch = None

        while not self.check(TokenType.END) and not self.check(TokenType.EOF):
            if self.match(TokenType.WHEN):
                self.expect(TokenType.LPAREN, "Expected '(' after WHEN")
                conditions = []
                while True:
                    c = self.parse_expression()
                    conditions.append(c)
                    if not self.match(TokenType.COMMA):
                        break
                self.expect(TokenType.RPAREN, "Expected ')' after WHEN conditions")
                stmts = self.parse_single_or_block()
                when_clauses.append((conditions, stmts))
            elif self.match(TokenType.OTHERWISE, TokenType.OTHER):
                otherwise_branch = self.parse_single_or_block()
            else:
                break

        self.expect(TokenType.END, "Expected END for SELECT")
        if self.check(TokenType.IDENT): self.advance()
        self.expect(TokenType.SEMICOLON, "Expected ';' after END")

        return ast.SelectStmt(expr=select_expr, when_clauses=when_clauses, otherwise_branch=otherwise_branch)

    def parse_call_stmt(self) -> ast.CallStmt:
        self.expect(TokenType.CALL)
        name = self.expect(TokenType.IDENT, "Expected procedure name after CALL").value
        args = []
        if self.match(TokenType.LPAREN):
            if not self.check(TokenType.RPAREN):
                while True:
                    args.append(self.parse_expression())
                    if not self.match(TokenType.COMMA):
                        break
            self.expect(TokenType.RPAREN, "Expected ')' after CALL arguments")
        self.expect(TokenType.SEMICOLON, "Expected ';' after CALL statement")
        return ast.CallStmt(callee=name, args=args)

    def parse_return(self) -> ast.ReturnStmt:
        self.expect(TokenType.RETURN)
        val = None
        if not self.check(TokenType.SEMICOLON):
            if self.match(TokenType.LPAREN):
                val = self.parse_expression()
                self.expect(TokenType.RPAREN, "Expected ')' after RETURN value")
            else:
                val = self.parse_expression()
        self.expect(TokenType.SEMICOLON, "Expected ';' after RETURN")
        return ast.ReturnStmt(value=val)

    def parse_put(self) -> ast.PutStmt:
        self.expect(TokenType.PUT)
        skip = False
        skip_count = None
        items = []
        formats = None

        while not self.check(TokenType.SEMICOLON) and not self.check(TokenType.EOF):
            if self.match(TokenType.SKIP):
                skip = True
                if self.match(TokenType.LPAREN):
                    skip_count = self.parse_expression()
                    self.expect(TokenType.RPAREN, "Expected ')' after SKIP count")
            elif self.match(TokenType.LINE, TokenType.PAGE):
                skip = True
                if self.match(TokenType.LPAREN):
                    self.parse_expression()
                    self.expect(TokenType.RPAREN)
            elif self.match(TokenType.LIST):
                self.expect(TokenType.LPAREN, "Expected '(' after LIST")
                if not self.check(TokenType.RPAREN):
                    while True:
                        items.append(self.parse_expression())
                        if not self.match(TokenType.COMMA):
                            break
                self.expect(TokenType.RPAREN, "Expected ')' after LIST items")
            elif self.match(TokenType.EDIT):
                self.expect(TokenType.LPAREN, "Expected '(' after EDIT")
                while True:
                    items.append(self.parse_expression())
                    if not self.match(TokenType.COMMA):
                        break
                self.expect(TokenType.RPAREN, "Expected ')' after EDIT items")
                self.expect(TokenType.LPAREN, "Expected '(' for format specifications")
                fmt_tokens = []
                paren_depth = 1
                while paren_depth > 0 and not self.check(TokenType.EOF):
                    t = self.advance()
                    if t.type == TokenType.LPAREN: paren_depth += 1
                    elif t.type == TokenType.RPAREN:
                        paren_depth -= 1
                        if paren_depth == 0: break
                    fmt_tokens.append(str(t.value))
                formats = [" ".join(fmt_tokens)]
            else:
                if self.match(TokenType.LPAREN):
                    while True:
                        items.append(self.parse_expression())
                        if not self.match(TokenType.COMMA):
                            break
                    self.expect(TokenType.RPAREN)
                else:
                    self.advance()

        self.expect(TokenType.SEMICOLON, "Expected ';' after PUT statement")
        return ast.PutStmt(skip=skip, skip_count=skip_count, items=items, formats=formats)

    def parse_get(self) -> ast.GetStmt:
        self.expect(TokenType.GET)
        if self.match(TokenType.SKIP):
            if self.match(TokenType.LPAREN):
                self.parse_expression()
                self.expect(TokenType.RPAREN)
        self.expect(TokenType.LIST, "Expected LIST in GET statement")
        self.expect(TokenType.LPAREN, "Expected '(' after LIST")
        items = []
        while True:
            items.append(self.parse_primary())
            if not self.match(TokenType.COMMA):
                break
        self.expect(TokenType.RPAREN, "Expected ')' after GET targets")
        self.expect(TokenType.SEMICOLON, "Expected ';' after GET statement")
        return ast.GetStmt(items=items)

    def parse_assignment(self) -> ast.AssignStmt:
        if self.check(TokenType.IDENT) and self.current().value == "SUBSTR" and self.peek().type == TokenType.LPAREN:
            self.advance()
            self.expect(TokenType.LPAREN)
            target = self.parse_expression()
            self.expect(TokenType.COMMA)
            start = self.parse_expression()
            length = None
            if self.match(TokenType.COMMA):
                length = self.parse_expression()
            self.expect(TokenType.RPAREN)
            substr_pseudo = ast.SubstrExpr(target=target, start=start, length=length)
            self.expect(TokenType.ASSIGN, "Expected '=' in SUBSTR assignment")
            val = self.parse_expression()
            self.expect(TokenType.SEMICOLON, "Expected ';' after assignment")
            return ast.AssignStmt(target=substr_pseudo, expr=val)

        target_name = self.expect(TokenType.IDENT, "Expected identifier in declaration or statement").value
        if self.match(TokenType.LPAREN):
            args = []
            while True:
                args.append(self.parse_expression())
                if not self.match(TokenType.COMMA):
                    break
            self.expect(TokenType.RPAREN, "Expected ')' after array indices")
            target = ast.CallOrIndexExpr(callee=target_name, args=args)
        else:
            target = ast.VarExpr(name=target_name)

        self.expect(TokenType.ASSIGN, f"Expected '=' after assignment target '{target_name}'")
        val = self.parse_expression()
        self.expect(TokenType.SEMICOLON, "Expected ';' after assignment")
        return ast.AssignStmt(target=target, expr=val)

    # Precedence Expression Parsing
    def parse_expression(self) -> ast.Expr:
        return self.parse_or()

    def parse_or(self) -> ast.Expr:
        expr = self.parse_and()
        while self.match(TokenType.OR):
            right = self.parse_and()
            expr = ast.BinaryExpr(op='|', left=expr, right=right)
        return expr

    def parse_and(self) -> ast.Expr:
        expr = self.parse_not()
        while self.match(TokenType.AND):
            right = self.parse_not()
            expr = ast.BinaryExpr(op='&', left=expr, right=right)
        return expr

    def parse_not(self) -> ast.Expr:
        if self.match(TokenType.NOT):
            operand = self.parse_not()
            return ast.UnaryExpr(op='^', expr=operand)
        return self.parse_comparison()

    def parse_comparison(self) -> ast.Expr:
        expr = self.parse_concat()
        while self.current().type in (TokenType.ASSIGN, TokenType.EQ, TokenType.NE,
                                      TokenType.LT, TokenType.LE, TokenType.GT, TokenType.GE):
            tok = self.advance()
            op = tok.value
            right = self.parse_concat()
            expr = ast.BinaryExpr(op=op, left=expr, right=right)
        return expr

    def parse_concat(self) -> ast.Expr:
        expr = self.parse_term()
        while self.match(TokenType.CONCAT):
            right = self.parse_term()
            expr = ast.BinaryExpr(op='||', left=expr, right=right)
        return expr

    def parse_term(self) -> ast.Expr:
        expr = self.parse_factor()
        while self.current().type in (TokenType.PLUS, TokenType.MINUS):
            op = self.advance().value
            right = self.parse_factor()
            expr = ast.BinaryExpr(op=op, left=expr, right=right)
        return expr

    def parse_factor(self) -> ast.Expr:
        expr = self.parse_power()
        while self.current().type in (TokenType.STAR, TokenType.SLASH):
            op = self.advance().value
            right = self.parse_power()
            expr = ast.BinaryExpr(op=op, left=expr, right=right)
        return expr

    def parse_power(self) -> ast.Expr:
        expr = self.parse_unary()
        if self.match(TokenType.POWER):
            right = self.parse_power()
            expr = ast.BinaryExpr(op='**', left=expr, right=right)
        return expr

    def parse_unary(self) -> ast.Expr:
        if self.current().type in (TokenType.PLUS, TokenType.MINUS):
            op = self.advance().value
            operand = self.parse_unary()
            return ast.UnaryExpr(op=op, expr=operand)
        return self.parse_primary()

    def parse_primary(self) -> ast.Expr:
        curr = self.current()

        if self.match(TokenType.INT_LIT):
            return ast.LiteralExpr(value=curr.value, lit_type='INT')

        if self.match(TokenType.FLOAT_LIT):
            return ast.LiteralExpr(value=curr.value, lit_type='FLOAT')

        if self.match(TokenType.STRING_LIT):
            return ast.LiteralExpr(value=curr.value, lit_type='STRING')

        if self.match(TokenType.BIT_LIT):
            return ast.LiteralExpr(value=curr.value, lit_type='BIT')

        if self.match(TokenType.LPAREN):
            expr = self.parse_expression()
            self.expect(TokenType.RPAREN, "Expected ')' after expression")
            return expr

        if self.match(TokenType.IDENT):
            name = curr.value
            if self.match(TokenType.LPAREN):
                if name == "SUBSTR":
                    target = self.parse_expression()
                    self.expect(TokenType.COMMA, "Expected ',' in SUBSTR")
                    start = self.parse_expression()
                    length = None
                    if self.match(TokenType.COMMA):
                        length = self.parse_expression()
                    self.expect(TokenType.RPAREN, "Expected ')' after SUBSTR")
                    return ast.SubstrExpr(target=target, start=start, length=length)

                args = []
                if not self.check(TokenType.RPAREN):
                    while True:
                        args.append(self.parse_expression())
                        if not self.match(TokenType.COMMA):
                            break
                self.expect(TokenType.RPAREN, "Expected ')' after arguments/subscripts")
                return ast.CallOrIndexExpr(callee=name, args=args)
            return ast.VarExpr(name=name)

        raise ParserError(f"Unexpected token in expression: {curr.type.name} ('{curr.value}')", curr)
