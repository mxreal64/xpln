from enum import Enum, auto

class TokenType(Enum):
    # End of file
    EOF = auto()

    # Identifiers and Literals
    IDENT = auto()
    INT_LIT = auto()
    FLOAT_LIT = auto()
    STRING_LIT = auto()
    BIT_LIT = auto()

    # Reserved Keywords
    PROCEDURE = auto()
    PROC = auto()
    END = auto()
    DECLARE = auto()
    DCL = auto()
    RETURNS = auto()
    OPTIONS = auto()
    MAIN = auto()
    IF = auto()
    THEN = auto()
    ELSE = auto()
    DO = auto()
    TO = auto()
    BY = auto()
    WHILE = auto()
    UNTIL = auto()
    SELECT = auto()
    WHEN = auto()
    OTHERWISE = auto()
    OTHER = auto()
    CALL = auto()
    RETURN = auto()
    GOTO = auto()
    GO = auto()
    STOP = auto()
    PUT = auto()
    GET = auto()
    SKIP = auto()
    LIST = auto()
    EDIT = auto()
    PAGE = auto()
    LINE = auto()
    INIT = auto()
    INITIAL = auto()

    # Types and Attributes
    FIXED = auto()
    BINARY = auto()
    BIN = auto()
    DECIMAL = auto()
    DEC = auto()
    FLOAT = auto()
    CHARACTER = auto()
    CHAR = auto()
    VARYING = auto()
    VAR = auto()
    BIT = auto()
    ENTRY = auto()

    # Operators
    PLUS = auto()         # +
    MINUS = auto()        # -
    STAR = auto()         # *
    SLASH = auto()        # /
    POWER = auto()        # **
    CONCAT = auto()       # ||
    ASSIGN = auto()       # =
    EQ = auto()           # = (in expression context)
    NE = auto()           # ^=, \=, !=, <>
    LT = auto()           # <
    LE = auto()           # <=
    GT = auto()           # >
    GE = auto()           # >=
    AND = auto()          # &
    OR = auto()           # |
    NOT = auto()          # ^, \, ~, !

    # Delimiters
    SEMICOLON = auto()    # ;
    COLON = auto()        # :
    COMMA = auto()        # ,
    LPAREN = auto()       # (
    RPAREN = auto()       # )

KEYWORDS = {
    'PROCEDURE': TokenType.PROCEDURE,
    'PROC': TokenType.PROC,
    'END': TokenType.END,
    'DECLARE': TokenType.DECLARE,
    'DCL': TokenType.DCL,
    'RETURNS': TokenType.RETURNS,
    'OPTIONS': TokenType.OPTIONS,
    'MAIN': TokenType.MAIN,
    'IF': TokenType.IF,
    'THEN': TokenType.THEN,
    'ELSE': TokenType.ELSE,
    'DO': TokenType.DO,
    'TO': TokenType.TO,
    'BY': TokenType.BY,
    'WHILE': TokenType.WHILE,
    'UNTIL': TokenType.UNTIL,
    'SELECT': TokenType.SELECT,
    'WHEN': TokenType.WHEN,
    'OTHERWISE': TokenType.OTHERWISE,
    'OTHER': TokenType.OTHER,
    'CALL': TokenType.CALL,
    'RETURN': TokenType.RETURN,
    'GOTO': TokenType.GOTO,
    'GO': TokenType.GO,
    'STOP': TokenType.STOP,
    'PUT': TokenType.PUT,
    'GET': TokenType.GET,
    'SKIP': TokenType.SKIP,
    'LIST': TokenType.LIST,
    'EDIT': TokenType.EDIT,
    'PAGE': TokenType.PAGE,
    'LINE': TokenType.LINE,
    'INIT': TokenType.INIT,
    'INITIAL': TokenType.INITIAL,

    # Data types / attributes
    'FIXED': TokenType.FIXED,
    'BINARY': TokenType.BINARY,
    'BIN': TokenType.BIN,
    'DECIMAL': TokenType.DECIMAL,
    'DEC': TokenType.DEC,
    'FLOAT': TokenType.FLOAT,
    'CHARACTER': TokenType.CHARACTER,
    'CHAR': TokenType.CHAR,
    'VARYING': TokenType.VARYING,
    'VAR': TokenType.VAR,
    'BIT': TokenType.BIT,
    'ENTRY': TokenType.ENTRY,
}

class Token:
    def __init__(self, type_: TokenType, value, line: int, col: int):
        self.type = type_
        self.value = value
        self.line = line
        self.col = col

    def __repr__(self):
        return f"Token({self.type.name}, {self.value!r}, line={self.line}, col={self.col})"
