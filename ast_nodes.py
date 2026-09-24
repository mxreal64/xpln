from dataclasses import dataclass
from typing import List, Optional, Any, Tuple

class ASTNode:
    pass

# Expressions
@dataclass
class Expr(ASTNode):
    pass

@dataclass
class LiteralExpr(Expr):
    value: Any
    lit_type: str  # 'INT', 'FLOAT', 'STRING', 'BIT'

@dataclass
class VarExpr(Expr):
    name: str

@dataclass
class UnaryExpr(Expr):
    op: str
    expr: Expr

@dataclass
class BinaryExpr(Expr):
    op: str
    left: Expr
    right: Expr

@dataclass
class CallOrIndexExpr(Expr):
    callee: str
    args: List[Expr]

@dataclass
class SubstrExpr(Expr):
    target: Expr
    start: Expr
    length: Optional[Expr]

# Declarations
@dataclass
class VarDecl:
    name: str
    data_type: str            # 'FIXED BIN', 'FLOAT', 'CHAR', 'BIT', etc.
    length: Optional[int] = None
    is_varying: bool = False
    bounds: Optional[List[Tuple[int, int]]] = None  # [(lower, upper), ...]
    init: Optional[Expr] = None

# Statements
@dataclass
class Stmt(ASTNode):
    pass

@dataclass
class DeclareStmt(Stmt):
    decls: List[VarDecl]

@dataclass
class AssignStmt(Stmt):
    target: Expr     # VarExpr, CallOrIndexExpr, or SubstrExpr
    expr: Expr

@dataclass
class IfStmt(Stmt):
    condition: Expr
    then_branch: List[Stmt]
    else_branch: Optional[List[Stmt]] = None

@dataclass
class DoGroupStmt(Stmt):
    body: List[Stmt]

@dataclass
class DoWhileStmt(Stmt):
    condition: Expr
    body: List[Stmt]

@dataclass
class DoUntilStmt(Stmt):
    condition: Expr
    body: List[Stmt]

@dataclass
class DoIterativeStmt(Stmt):
    var_name: str
    start: Expr
    to: Expr
    by: Optional[Expr]
    while_cond: Optional[Expr]
    until_cond: Optional[Expr]
    body: List[Stmt]

@dataclass
class SelectStmt(Stmt):
    expr: Optional[Expr]
    when_clauses: List[Tuple[List[Expr], List[Stmt]]]
    otherwise_branch: Optional[List[Stmt]] = None

@dataclass
class CallStmt(Stmt):
    callee: str
    args: List[Expr]

@dataclass
class ReturnStmt(Stmt):
    value: Optional[Expr] = None

@dataclass
class StopStmt(Stmt):
    pass

@dataclass
class PutStmt(Stmt):
    skip: bool
    skip_count: Optional[Expr]
    items: List[Expr]
    formats: Optional[List[str]] = None

@dataclass
class GetStmt(Stmt):
    items: List[Expr]

@dataclass
class ProcedureDecl(Stmt):
    name: str
    params: List[str]
    returns_type: Optional[str]
    is_main: bool
    body: List[Stmt]

@dataclass
class Program(ASTNode):
    procedures: List[ProcedureDecl]
