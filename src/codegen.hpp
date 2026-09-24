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

        // gen_expr returns true if result was loaded into %xmm0 (float), false if in %rax (int/ptr)
        bool gen_expr(const ast::ExprPtr& expr);
        bool is_float_expr(const ast::ExprPtr& expr);
    };

} // namespace xpln

#endif // XPLN_CODEGEN_HPP
