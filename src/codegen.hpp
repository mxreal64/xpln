#pragma once

#include "ast_nodes.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <format>

namespace xpln {

    struct LocalSymbol {
        int stack_offset;
        std::string type;
    };

    struct RodataString {
        std::string label;
        std::string value;
    };

    class CodeGenerator {
    public:
        CodeGenerator() = default;

        std::string generate(const ast::Program& program);

    private:
        void emit_label(const std::string& label);
        void emit_inst(const std::string& op, const std::string& args = "");
        void emit_comment(const std::string& comment);

        void gen_procedure(const ast::ProcedureDecl& proc);
        void gen_stmt(const ast::StmtPtr& stmt);
        void gen_expr(const ast::ExprPtr& expr);

        std::string add_string_literal(const std::string& str);
        std::string get_label(const std::string& prefix = ".L");
        int allocate_stack_space(int bytes = 8);

        int indent_level_ = 1;
        std::vector<std::string> lines_;
        std::vector<RodataString> rodata_strings_;

        int label_counter_ = 0;
        int string_counter_ = 0;
        int current_stack_offset_ = 0;

        std::unordered_map<std::string, LocalSymbol> symbol_table_;
    };

} // namespace xpln
