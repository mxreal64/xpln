#ifndef XPLN_CODEGEN_HPP
#define XPLN_CODEGEN_HPP

#include "ast_nodes.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace xpln {

    enum class TypeKind {
        Int,
        Float,
        String,
        RefInt,
        RefFloat
    };

    struct LocalSymbol {
        int stack_offset;
        TypeKind kind;
        std::string raw_type;
        std::vector<ast::ArrayBound> bounds;
    };

    struct StringLiteral {
        std::string label;
        std::string value;
    };

    struct DoubleLiteral {
        std::string label;
        double value;
    };

    class CodeGenerator {
    public:
        std::string generate(const ast::Program& program);

    private:
        std::vector<std::string> lines_;
        std::vector<StringLiteral> rodata_strings_;
        std::vector<DoubleLiteral> rodata_doubles_;
        std::unordered_map<std::string, LocalSymbol> symbol_table_;
        // Populated once per generate() call by scanning every procedure's
        // RETURNS(...) clause, keyed by lower-cased procedure name. Lets a
        // call site (which only sees the callee's name) know whether the
        // result comes back in %xmm0 (float) or %rax (int/ptr) — mirrors
        // symbol_table_'s role for variables.
        std::unordered_map<std::string, bool> proc_returns_float_;
        int label_counter_ = 0;
        int string_counter_ = 0;
        int double_counter_ = 0;
        int current_stack_offset_ = 0;

        void emit_inst(const std::string& op, const std::string& args = "");
        void emit_label(const std::string& label);
        void emit_comment(const std::string& comment);
        std::string get_label(const std::string& prefix);
        std::string add_string_literal(const std::string& str);
        std::string add_double_literal(double val);
        int allocate_stack_space(int bytes = 8);

        void gen_procedure(const ast::ProcedureDecl& proc);
        void gen_stmt(const ast::StmtPtr& stmt);
        void gen_array_element_address(const LocalSymbol& sym, const std::vector<ast::ExprPtr>& args);
        // Leaves the ADDRESS of `expr` in %rax, materializing a fresh stack
        // temporary first if `expr` isn't itself addressable storage (e.g.
        // a literal or a computed sub-expression). Used at call sites: PL/I
        // procedure parameters are BY REFERENCE by default, so every actual
        // argument must be passed as a pointer, never a bare value.
        void gen_arg_address(const ast::ExprPtr& expr);
        void gen_when_condition(bool has_select_expr, bool select_is_float, int select_stack_offset,
                                const ast::ExprPtr& cond, const std::string& match_label);

        // gen_expr returns true if result was loaded into %xmm0 (float), false if in %rax (int/ptr)
        bool gen_expr(const ast::ExprPtr& expr);
        bool is_float_expr(const ast::ExprPtr& expr);
    };

} // namespace xpln

#endif // XPLN_CODEGEN_HPP
