#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xpln::ast {

    // Forward Declarations
    struct LiteralExpr;
    struct VarExpr;
    struct UnaryExpr;
    struct BinaryExpr;
    struct CallOrIndexExpr;
    struct SubstrExpr;

    using ExprPtr = std::unique_ptr<struct Expr>;

    struct LiteralExpr {
        enum class Type { Int, Float, String, Bit };
        std::string value;
        Type lit_type;
    };

    struct VarExpr {
        std::string name;
    };

    struct UnaryExpr {
        std::string op;
        ExprPtr expr;
    };

    struct BinaryExpr {
        std::string op;
        ExprPtr left;
        ExprPtr right;
    };

    struct CallOrIndexExpr {
        std::string callee;
        std::vector<ExprPtr> args;
    };

    struct SubstrExpr {
        ExprPtr target;
        ExprPtr start;
        ExprPtr length; // Can be nullptr
    };

    using ExprData = std::variant<
    LiteralExpr,
    VarExpr,
    UnaryExpr,
    BinaryExpr,
    CallOrIndexExpr,
    SubstrExpr
    >;

    struct Expr {
        ExprData data;

        template <typename T>
        requires (!std::same_as<std::decay_t<T>, Expr>)
        explicit Expr(T&& val) : data(std::forward<T>(val)) {}

        Expr(Expr&&) noexcept = default;
        Expr& operator=(Expr&&) noexcept = default;
        Expr(const Expr&) = delete;
        Expr& operator=(const Expr&) = delete;
    };

    // Deep copy helper for AST expressions
    inline ExprPtr clone_expr(const ExprPtr& src) {
        if (!src) return nullptr;

        return std::visit([](const auto& e) -> ExprPtr {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, LiteralExpr>) {
                return std::make_unique<Expr>(LiteralExpr{e.value, e.lit_type});
            } else if constexpr (std::is_same_v<T, VarExpr>) {
                return std::make_unique<Expr>(VarExpr{e.name});
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return std::make_unique<Expr>(UnaryExpr{e.op, clone_expr(e.expr)});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return std::make_unique<Expr>(BinaryExpr{e.op, clone_expr(e.left), clone_expr(e.right)});
            } else if constexpr (std::is_same_v<T, CallOrIndexExpr>) {
                std::vector<ExprPtr> args;
                for (const auto& arg : e.args) args.push_back(clone_expr(arg));
                return std::make_unique<Expr>(CallOrIndexExpr{e.callee, std::move(args)});
            } else if constexpr (std::is_same_v<T, SubstrExpr>) {
                return std::make_unique<Expr>(SubstrExpr{
                    clone_expr(e.target),
                                              clone_expr(e.start),
                                              clone_expr(e.length)
                });
            }
        }, src->data);
    }

    // Declarations & Statements
    struct ProcedureDecl;
    using StmtPtr = std::unique_ptr<struct Stmt>;

    struct ArrayBound {
        int low;
        int high;
    };

    struct VarDecl {
        std::string name;
        std::string data_type;
        std::optional<int> length;
        bool is_varying{false};
        std::optional<std::vector<ArrayBound>> bounds;
        ExprPtr init;
    };

    struct DeclareStmt {
        std::vector<VarDecl> decls;
    };

    struct AssignStmt {
        ExprPtr target;
        ExprPtr expr;
    };

    struct IfStmt {
        ExprPtr condition;
        std::vector<StmtPtr> then_branch;
        std::vector<StmtPtr> else_branch;
    };

    struct DoGroupStmt {
        std::vector<StmtPtr> body;
    };

    struct DoWhileStmt {
        ExprPtr condition;
        std::vector<StmtPtr> body;
    };

    struct DoUntilStmt {
        ExprPtr condition;
        std::vector<StmtPtr> body;
    };

    struct DoIterativeStmt {
        std::string var_name;
        ExprPtr start;
        ExprPtr to;
        ExprPtr by;
        ExprPtr while_cond;
        ExprPtr until_cond;
        std::vector<StmtPtr> body;
    };

    struct SelectStmt {
        ExprPtr expr;
        std::vector<std::pair<std::vector<ExprPtr>, std::vector<StmtPtr>>> when_clauses;
        std::vector<StmtPtr> otherwise_branch;
    };

    struct CallStmt {
        std::string callee;
        std::vector<ExprPtr> args;
    };

    struct ReturnStmt {
        ExprPtr value;
    };

    struct StopStmt {};

    struct PutStmt {
        bool skip{false};
        ExprPtr skip_count;
        std::vector<ExprPtr> items;
        std::optional<std::vector<std::string>> formats;
    };

    struct GetStmt {
        std::vector<ExprPtr> items;
    };

    struct ProcedureDecl {
        std::string name;
        std::vector<std::string> params;
        std::optional<std::string> returns_type;
        bool is_main{false};
        std::vector<StmtPtr> body;
    };

    using StmtData = std::variant<
    DeclareStmt,
    AssignStmt,
    IfStmt,
    DoGroupStmt,
    DoWhileStmt,
    DoUntilStmt,
    DoIterativeStmt,
    SelectStmt,
    CallStmt,
    ReturnStmt,
    StopStmt,
    PutStmt,
    GetStmt,
    ProcedureDecl
    >;

    struct Stmt {
        StmtData data;

        template <typename T>
        requires (!std::same_as<std::decay_t<T>, Stmt>)
        explicit Stmt(T&& val) : data(std::forward<T>(val)) {}

        Stmt(Stmt&&) noexcept = default;
        Stmt& operator=(Stmt&&) noexcept = default;
        Stmt(const Stmt&) = delete;
        Stmt& operator=(const Stmt&) = delete;
    };

    struct Program {
        std::vector<ProcedureDecl> procedures;
    };

} // namespace xpln::ast
