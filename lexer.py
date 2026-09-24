import re
from tokens import TokenType, Token, KEYWORDS

class LexerError(Exception):
    def __init__(self, message: str, line: int, col: int):
        super().__init__(f"Lexer error at line {line}, col {col}: {message}")
        self.line = line
        self.col = col

class Lexer:
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.col = 1
        self.length = len(source)

    def peek(self, offset: int = 0) -> str:
        idx = self.pos + offset
        if idx < self.length:
            return self.source[idx]
        return '\0'

    def advance(self) -> str:
        ch = self.peek()
        self.pos += 1
        if ch == '\n':
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def match(self, expected: str) -> bool:
        if self.source.startswith(expected, self.pos):
            for _ in expected:
                self.advance()
            return True
        return False

    def skip_whitespace_and_comments(self):
        while self.pos < self.length:
            ch = self.peek()
            if ch in ' \t\r\n':
                self.advance()
            elif ch == '/' and self.peek(1) == '*':
                # Multi-line comment /* ... */
                start_line, start_col = self.line, self.col
                self.advance()  # /
                self.advance()  # *
                closed = False
                while self.pos < self.length:
                    if self.peek() == '*' and self.peek(1) == '/':
                        self.advance()
                        self.advance()
                        closed = True
                        break
                    self.advance()
                if not closed:
                    raise LexerError("Unterminated comment /*", start_line, start_col)
            elif ch == '/' and self.peek(1) == '/':
                # Single-line comment // ...
                self.advance()
                self.advance()
                while self.pos < self.length and self.peek() != '\n':
                    self.advance()
            else:
                break

    def tokenize(self):
        tokens = []
        while True:
            self.skip_whitespace_and_comments()
            if self.pos >= self.length:
                tokens.append(Token(TokenType.EOF, '', self.line, self.col))
                break

            start_line, start_col = self.line, self.col
            ch = self.peek()

            # Multi-character operators
            if ch == '*' and self.peek(1) == '*':
                self.advance(); self.advance()
                tokens.append(Token(TokenType.POWER, '**', start_line, start_col))
            elif ch == '|' and self.peek(1) == '|':
                self.advance(); self.advance()
                tokens.append(Token(TokenType.CONCAT, '||', start_line, start_col))
            elif (ch in '^\\~!' and self.peek(1) == '=') or (ch == '!' and self.peek(1) == '='):
                self.advance(); self.advance()
                tokens.append(Token(TokenType.NE, '^=', start_line, start_col))
            elif ch == '<' and self.peek(1) == '=':
                self.advance(); self.advance()
                tokens.append(Token(TokenType.LE, '<=', start_line, start_col))
            elif ch == '<' and self.peek(1) == '>':
                self.advance(); self.advance()
                tokens.append(Token(TokenType.NE, '^=', start_line, start_col))
            elif ch == '>' and self.peek(1) == '=':
                self.advance(); self.advance()
                tokens.append(Token(TokenType.GE, '>=', start_line, start_col))
            # Single character operators & delimiters
            elif ch == '+':
                self.advance()
                tokens.append(Token(TokenType.PLUS, '+', start_line, start_col))
            elif ch == '-':
                self.advance()
                tokens.append(Token(TokenType.MINUS, '-', start_line, start_col))
            elif ch == '*':
                self.advance()
                tokens.append(Token(TokenType.STAR, '*', start_line, start_col))
            elif ch == '/':
                self.advance()
                tokens.append(Token(TokenType.SLASH, '/', start_line, start_col))
            elif ch == '=':
                self.advance()
                tokens.append(Token(TokenType.ASSIGN, '=', start_line, start_col))
            elif ch == '<':
                self.advance()
                tokens.append(Token(TokenType.LT, '<', start_line, start_col))
            elif ch == '>':
                self.advance()
                tokens.append(Token(TokenType.GT, '>', start_line, start_col))
            elif ch == '&':
                self.advance()
                tokens.append(Token(TokenType.AND, '&', start_line, start_col))
            elif ch == '|':
                self.advance()
                tokens.append(Token(TokenType.OR, '|', start_line, start_col))
            elif ch in '^\\~!':
                self.advance()
                tokens.append(Token(TokenType.NOT, '^', start_line, start_col))
            elif ch == ';':
                self.advance()
                tokens.append(Token(TokenType.SEMICOLON, ';', start_line, start_col))
            elif ch == ':':
                self.advance()
                tokens.append(Token(TokenType.COLON, ':', start_line, start_col))
            elif ch == ',':
                self.advance()
                tokens.append(Token(TokenType.COMMA, ',', start_line, start_col))
            elif ch == '(':
                self.advance()
                tokens.append(Token(TokenType.LPAREN, '(', start_line, start_col))
            elif ch == ')':
                self.advance()
                tokens.append(Token(TokenType.RPAREN, ')', start_line, start_col))
            # String literals: '...'
            elif ch == "'":
                self.advance()
                s = []
                while True:
                    if self.pos >= self.length:
                        raise LexerError("Unterminated string literal", start_line, start_col)
                    c = self.advance()
                    if c == "'":
                        if self.peek() == "'":
                            s.append("'")
                            self.advance()
                        else:
                            break
                    else:
                        s.append(c)
                string_val = "".join(s)
                # Check for bit literal suffix 'B' or 'b'
                if self.peek().upper() == 'B' and not (self.peek(1).isalnum() or self.peek(1) == '_'):
                    self.advance()
                    clean_bits = string_val.replace(' ', '')
                    if not all(b in '01' for b in clean_bits):
                        raise LexerError(f"Invalid bit string content '{string_val}'", start_line, start_col)
                    tokens.append(Token(TokenType.BIT_LIT, clean_bits, start_line, start_col))
                else:
                    tokens.append(Token(TokenType.STRING_LIT, string_val, start_line, start_col))
            # Numeric literals
            elif ch.isdigit() or (ch == '.' and self.peek(1).isdigit()):
                num_str = []
                is_float = False
                if ch == '.':
                    is_float = True
                    num_str.append(self.advance())

                while self.peek().isdigit():
                    num_str.append(self.advance())

                if self.peek() == '.' and not is_float:
                    is_float = True
                    num_str.append(self.advance())
                    while self.peek().isdigit():
                        num_str.append(self.advance())

                if self.peek() in 'eE':
                    is_float = True
                    num_str.append(self.advance())
                    if self.peek() in '+-':
                        num_str.append(self.advance())
                    if not self.peek().isdigit():
                        raise LexerError("Exponent has no digits", start_line, start_col)
                    while self.peek().isdigit():
                        num_str.append(self.advance())

                val_text = "".join(num_str)
                if is_float:
                    tokens.append(Token(TokenType.FLOAT_LIT, float(val_text), start_line, start_col))
                else:
                    tokens.append(Token(TokenType.INT_LIT, int(val_text), start_line, start_col))
            # Identifiers and Reserved Keywords
            elif ch.isalpha() or ch in '_$#@':
                ident = []
                while self.peek().isalnum() or self.peek() in '_$#@':
                    ident.append(self.advance())
                word = "".join(ident)
                upper_word = word.upper()
                if upper_word in KEYWORDS:
                    tokens.append(Token(KEYWORDS[upper_word], upper_word, start_line, start_col))
                else:
                    tokens.append(Token(TokenType.IDENT, upper_word, start_line, start_col))
            else:
                raise LexerError(f"Unexpected character: {ch!r}", start_line, start_col)

        return tokens
