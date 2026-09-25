#include "codegen.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <format>

namespace xpln {

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    // NOTE: this used to special-case a hardcoded list of variable names
    // ("pi", "radius", "area", ...) to force float typing — a workaround for
    // a symptom, not the cause. The general path below already classifies
    // any `DCL x FLOAT;` correctly, and PASS 1 in gen_procedure()
    // re-classifies a by-reference parameter's kind once its real DCL is
    // seen in the body (verified with `DCL x FLOAT;` and a by-reference
    // float parameter under names *not* on that list). Deleting the hack
    // fixes float variables that aren't spelled those exact six ways.
    static TypeKind parse_type_kind(const std::string& type_str, bool is_ref = false) {
        std::string t = type_str;
        std::transform(t.begin(), t.end(), t.begin(), ::toupper);

        if (t.find("FLOAT") != std::string::npos ||
            t.find("DOUBLE") != std::string::npos ||
            t.find("REAL") != std::string::npos) {
            return is_ref ? TypeKind::RefFloat : TypeKind::Float;
            }
            if (t.find("CHAR") != std::string::npos || t.find("STRING") != std::string::npos) {
        return TypeKind::String;
            }
    return is_ref ? TypeKind::RefInt : TypeKind::Int;
    }

    // PL/I math built-ins that bind straight to libm, per the Phase 1 plan
    // (ROADMAP.md). Unlike user-defined PL/I procedures (by-reference, see
    // gen_arg_address), these follow the plain C/SysV ABI: argument by
    // VALUE in %xmm0, result by value in %xmm0. ABS is handled separately
    // below since it must stay int-valued for an int argument.
    static const char* builtin_math_fn(const std::string& callee) {
        std::string upper = callee;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper == "SQRT") return "sqrt";
        if (upper == "SIN") return "sin";
        if (upper == "COS") return "cos";
        if (upper == "LOG") return "log";
        return nullptr;
    }

void CodeGenerator::emit_label(const std::string& label) {
    lines_.push_back(std::format("{}:", label));
}

void CodeGenerator::emit_inst(const std::string& op, const std::string& args) {
    if (args.empty()) {
        lines_.push_back(std::format("\t{}", op));
    } else {
        lines_.push_back(std::format("\t{:<8} {}", op, args));
    }
}

void CodeGenerator::emit_comment(const std::string& comment) {
    lines_.push_back(std::format("\t# {}", comment));
}

std::string CodeGenerator::get_label(const std::string& prefix) {
    return std::format("{}{}", prefix, ++label_counter_);
}

std::string CodeGenerator::add_string_literal(const std::string& str) {
    std::string lbl = std::format(".LC_str_{}", ++string_counter_);
    rodata_strings_.push_back({lbl, str});
    return lbl;
}

std::string CodeGenerator::add_double_literal(double val) {
    std::string lbl = std::format(".LC_dbl_{}", ++double_counter_);
    rodata_doubles_.push_back({lbl, val});
    return lbl;
}

int CodeGenerator::allocate_stack_space(int bytes) {
    current_stack_offset_ += bytes;
    return -current_stack_offset_;
}

bool CodeGenerator::is_float_expr(const ast::ExprPtr& expr) {
    if (!expr) return false;

    return std::visit([this](auto&& e) -> bool {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
            return e.lit_type == ast::LiteralExpr::Type::Float;
        }
        else if constexpr (std::is_same_v<T, ast::VarExpr>) {
            auto it = symbol_table_.find(to_lower(e.name));
            if (it != symbol_table_.end()) {
                return (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
            }
            return false;
        }
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            return is_float_expr(e.left) || is_float_expr(e.right);
        }
        else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            return is_float_expr(e.expr);
        }
        else if constexpr (std::is_same_v<T, ast::CallOrIndexExpr>) {
            auto it = symbol_table_.find(to_lower(e.callee));
            if (it != symbol_table_.end()) {
                return (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
            }
            return false;
        }
        return false;
    }, expr->data);
}

void CodeGenerator::gen_array_element_address(const LocalSymbol& sym, const std::vector<ast::ExprPtr>& args) {
    // Row-major, last subscript fastest-varying: stride[i] = product of
    // extents of all dimensions after i. Falls back to the historical
    // single-index/low=1/stride=1 behavior if bounds are unknown (keeps a
    // scalar mistakenly indexed from crashing the compiler).
    std::vector<int> low, stride;
    if (sym.bounds.empty()) {
        low = {1};
        stride = {1};
    } else {
        low.resize(sym.bounds.size());
        stride.resize(sym.bounds.size());
        int running = 1;
        for (std::size_t i = sym.bounds.size(); i-- > 0;) {
            low[i] = sym.bounds[i].low;
            stride[i] = running;
            running *= (sym.bounds[i].high - sym.bounds[i].low + 1);
        }
    }

    const std::size_t n = std::min(args.size(), low.size());

    int acc_offset = allocate_stack_space(8);
    emit_inst("movq", std::format("$0, {}(%rbp)", acc_offset));

    for (std::size_t i = 0; i < n; ++i) {
        gen_expr(args[i]); // -> %rax (index value); subscript expressions may
        // freely recurse (including into this same
        // helper for nested indexing) since the running
        // total lives on the stack, not in a register.
        emit_inst("subq", std::format("${}, %rax", low[i]));
        emit_inst("imulq", std::format("${}, %rax", stride[i]));
        emit_inst("addq", std::format("{}(%rbp), %rax", acc_offset));
        emit_inst("movq", std::format("%rax, {}(%rbp)", acc_offset));
    }

    emit_inst("movq", std::format("{}(%rbp), %rcx", acc_offset));
    emit_inst("imulq", "$8, %rcx");
    emit_inst("leaq", std::format("{}(%rbp), %rax", sym.stack_offset));
    emit_inst("subq", "%rcx, %rax");
}

void CodeGenerator::gen_arg_address(const ast::ExprPtr& expr) {
    if (expr) {
        if (auto* var = std::get_if<ast::VarExpr>(&expr->data)) {
            auto it = symbol_table_.find(to_lower(var->name));
            if (it != symbol_table_.end()) {
                bool is_ref = (it->second.kind == TypeKind::RefInt || it->second.kind == TypeKind::RefFloat);
                if (is_ref) {
                    // Already a pointer to the real storage (this is itself
                    // a by-reference parameter) — forward it as-is rather
                    // than taking "the address of the address".
                    emit_inst("movq", std::format("{}(%rbp), %rax", it->second.stack_offset));
                } else {
                    emit_inst("leaq", std::format("{}(%rbp), %rax", it->second.stack_offset));
                }
                return;
            }
        } else if (auto* call_expr = std::get_if<ast::CallOrIndexExpr>(&expr->data)) {
            auto it = symbol_table_.find(to_lower(call_expr->callee));
            if (it != symbol_table_.end()) {
                gen_array_element_address(it->second, call_expr->args); // -> %rax = element address
                return;
            }
        }
    }

    // Not addressable storage (a literal, or the result of a computed
    // sub-expression): evaluate it and spill to a fresh stack temp, then
    // hand back that temp's address instead.
    bool is_float = gen_expr(expr);
    int temp_offset = allocate_stack_space(8);
    if (is_float) {
        emit_inst("movsd", std::format("%xmm0, {}(%rbp)", temp_offset));
    } else {
        emit_inst("movq", std::format("%rax, {}(%rbp)", temp_offset));
    }
    emit_inst("leaq", std::format("{}(%rbp), %rax", temp_offset));
}

void CodeGenerator::gen_when_condition(bool has_select_expr, bool select_is_float, int select_stack_offset,
                                       const ast::ExprPtr& cond, const std::string& match_label) {
    bool cond_is_float = gen_expr(cond); // -> %rax or %xmm0

    if (!has_select_expr) {
        // Bare SELECT: the condition is itself a boolean ("nonzero").
        if (cond_is_float) {
            std::string zero_lbl = add_double_literal(0.0);
            emit_inst("movsd", std::format("{}(%rip), %xmm1", zero_lbl));
            emit_inst("ucomisd", "%xmm1, %xmm0");
        } else {
            emit_inst("cmpq", "$0, %rax");
        }
        emit_inst("jne", match_label);
        return;
    }

    // SELECT(expr): compare this WHEN value against the spilled select
    // value for equality, promoting int -> float if either side is float.
    if (select_is_float || cond_is_float) {
        if (!cond_is_float) {
            emit_inst("cvtsi2sdq", "%rax, %xmm0");
        }
        if (select_is_float) {
            emit_inst("movsd", std::format("{}(%rbp), %xmm1", select_stack_offset));
        } else {
            emit_inst("movq", std::format("{}(%rbp), %rax", select_stack_offset));
            emit_inst("cvtsi2sdq", "%rax, %xmm1");
        }
        emit_inst("ucomisd", "%xmm0, %xmm1");
        emit_inst("je", match_label);
    } else {
        emit_inst("movq", std::format("{}(%rbp), %rcx", select_stack_offset));
        emit_inst("cmpq", "%rax, %rcx");
        emit_inst("je", match_label);
    }
                                       }

                                       std::string CodeGenerator::generate(const ast::Program& program) {
                                           lines_.clear();
                                           rodata_strings_.clear();
                                           rodata_doubles_.clear();
                                           label_counter_ = 0;
                                           string_counter_ = 0;
                                           double_counter_ = 0;

                                           lines_.push_back("\t.text");

                                           proc_returns_float_.clear();
                                           for (const auto& proc : program.procedures) {
                                               bool returns_float = false;
                                               if (proc.returns_type) {
                                                   std::string t = *proc.returns_type;
                                                   std::transform(t.begin(), t.end(), t.begin(), ::toupper);
                                                   returns_float = t.find("FLOAT") != std::string::npos ||
                                                   t.find("DOUBLE") != std::string::npos ||
                                                   t.find("REAL") != std::string::npos;
                                               }
                                               proc_returns_float_[to_lower(proc.name)] = returns_float;
                                           }

                                           for (const auto& proc : program.procedures) {
                                               gen_procedure(proc);
                                           }

                                           std::vector<std::string> rodata_lines;
                                           rodata_lines.push_back("\t.section .rodata");
                                           rodata_lines.push_back(".LC_fmt_int:\t.string \"%ld \"");
                                           rodata_lines.push_back(".LC_fmt_float:\t.string \"%.2f \"");
                                           rodata_lines.push_back(".LC_fmt_str:\t.string \"%s \"");
                                           rodata_lines.push_back(".LC_newline:\t.string \"\\n\"");
                                           rodata_lines.push_back(".LC_scan_int:\t.string \"%ld\"");
                                           rodata_lines.push_back(".LC_scan_float:\t.string \"%lf\"");
                                           rodata_lines.push_back(".LC_scan_str:\t.string \"%255s\"");

                                           for (const auto& s : rodata_strings_) {
                                               rodata_lines.push_back(std::format("{}:\t.string \"{}\"", s.label, s.value));
                                           }

                                           for (const auto& d : rodata_doubles_) {
                                               rodata_lines.push_back(std::format("{}:\t.double {}", d.label, d.value));
                                           }

                                           std::string result;
                                           for (const auto& line : rodata_lines) result += line + "\n";
                                           for (const auto& line : lines_) result += line + "\n";

                                           return result;
                                       }

                                       void CodeGenerator::gen_procedure(const ast::ProcedureDecl& proc) {
                                           auto parent_symbols = symbol_table_;
                                           int parent_offset = current_stack_offset_;
                                           current_stack_offset_ = 0;

                                           std::string symbol_name = proc.is_main ? "main" : std::format("pli_{}", proc.name);
                                           std::transform(symbol_name.begin(), symbol_name.end(), symbol_name.begin(), ::tolower);

                                           lines_.push_back(std::format("\t.globl {}", symbol_name));
                                           lines_.push_back(std::format("\t.type {}, @function", symbol_name));
                                           emit_label(symbol_name);

                                           emit_inst("pushq", "%rbp");
                                           emit_inst("movq", "%rsp, %rbp");

                                           std::size_t stack_sub_idx = lines_.size();
                                           lines_.push_back("");

                                           static const char* arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
                                           for (size_t i = 0; i < proc.params.size() && i < 6; ++i) {
                                               std::string param_name = to_lower(proc.params[i]);
                                               int offset = allocate_stack_space(8);
                                               // Real type is unknown until (if) the body re-declares this
                                               // parameter with DCL — PASS 1 below re-classifies it then. Default
                                               // to RefInt in the meantime.
                                               symbol_table_[param_name] = LocalSymbol{offset, TypeKind::RefInt, "REF", {}};
                                               emit_inst("movq", std::format("{}, {}(%rbp)", arg_regs[i], offset));
                                           }

                                           // PASS 1: Pre-register symbol types and stack locations
                                           for (const auto& stmt : proc.body) {
                                               if (!stmt) continue;
                                               if (auto* decl_stmt = std::get_if<ast::DeclareStmt>(&stmt->data)) {
                                                   for (const auto& decl : decl_stmt->decls) {
                                                       std::string name_key = to_lower(decl.name);
                                                       auto existing = symbol_table_.find(name_key);

                                                       TypeKind kind = parse_type_kind(decl.data_type, existing != symbol_table_.end());

                                                       if (existing != symbol_table_.end()) {
                                                           existing->second.kind = kind;
                                                           existing->second.raw_type = decl.data_type;
                                                       } else {
                                                           int total_slots = 1;
                                                           if (decl.bounds) {
                                                               for (const auto& b : *decl.bounds) {
                                                                   total_slots *= (b.high - b.low + 1);
                                                               }
                                                           }

                                                           int first_slot_offset = -(current_stack_offset_ + 8);
                                                           allocate_stack_space(8 * total_slots);
                                                           std::vector<ast::ArrayBound> bounds;
                                                           if (decl.bounds) bounds = *decl.bounds;
                                                           symbol_table_[name_key] = LocalSymbol{first_slot_offset, kind, decl.data_type, std::move(bounds)};
                                                       }
                                                   }
                                               }
                                           }

                                           // PASS 2: Code Generation
                                           for (const auto& stmt : proc.body) {
                                               gen_stmt(stmt);
                                           }

                                           int aligned_stack_size = (current_stack_offset_ + 15) & ~15;
                                           if (aligned_stack_size > 0) {
                                               lines_[stack_sub_idx] = std::format("\tsubq     ${}, %rsp", aligned_stack_size);
                                           } else {
                                               lines_[stack_sub_idx] = "\t# No local stack allocation needed";
                                           }

                                           if (proc.is_main) {
                                               emit_inst("movq", "$0, %rax");
                                           }
                                           emit_inst("movq", "%rbp, %rsp");
                                           emit_inst("popq", "%rbp");
                                           emit_inst("ret");

                                           symbol_table_ = parent_symbols;
                                           current_stack_offset_ = parent_offset;
                                       }

                                       void CodeGenerator::gen_stmt(const ast::StmtPtr& stmt) {
                                           if (!stmt) return;

                                           std::visit([this](auto&& s) {
                                               using T = std::decay_t<decltype(s)>;

                                               if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
                                                   // Nested procedures are only ever meant to be reached
                                                   // via CALL/its label, never by falling into them —
                                                   // jump around the body so normal control flow in the
                                                   // enclosing procedure doesn't execute the nested
                                                   // procedure's prologue a second time (outside of any
                                                   // real `call`, corrupting the stack when its `ret`
                                                   // pops a bogus "return address").
                                                   std::string skip_label = get_label(".L_skip_nested_");
                                                   emit_inst("jmp", skip_label);
                                                   gen_procedure(s);
                                                   emit_label(skip_label);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::DeclareStmt>) {
                                                   for (const auto& decl : s.decls) {
                                                       if (decl.init) {
                                                           std::string name_key = to_lower(decl.name);
                                                           auto it = symbol_table_.find(name_key);
                                                           if (it != symbol_table_.end()) {
                                                               bool is_float = gen_expr(decl.init);
                                                               bool target_is_float = (it->second.kind == TypeKind::Float);
                                                               if (target_is_float && !is_float) {
                                                                   emit_inst("cvtsi2sdq", "%rax, %xmm0");
                                                               } else if (!target_is_float && is_float) {
                                                                   emit_inst("cvttsd2siq", "%xmm0, %rax");
                                                               }
                                                               if (target_is_float) {
                                                                   emit_inst("movsd", std::format("%xmm0, {}(%rbp)", it->second.stack_offset));
                                                               } else {
                                                                   emit_inst("movq", std::format("%rax, {}(%rbp)", it->second.stack_offset));
                                                               }
                                                           }
                                                       }
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
                                                   bool rhs_is_float = gen_expr(s.expr);

                                                   if (auto* var_expr = std::get_if<ast::VarExpr>(&s.target->data)) {
                                                       auto it = symbol_table_.find(to_lower(var_expr->name));
                                                       if (it != symbol_table_.end()) {
                                                           bool target_is_float = (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
                                                           bool is_ref = (it->second.kind == TypeKind::RefInt || it->second.kind == TypeKind::RefFloat);

                                                           if (target_is_float && !rhs_is_float) {
                                                               emit_inst("cvtsi2sdq", "%rax, %xmm0");
                                                           } else if (!target_is_float && rhs_is_float) {
                                                               emit_inst("cvttsd2siq", "%xmm0, %rax");
                                                           }

                                                           if (is_ref) {
                                                               emit_inst("movq", std::format("{}(%rbp), %rcx", it->second.stack_offset));
                                                               if (target_is_float) {
                                                                   emit_inst("movsd", "%xmm0, (%rcx)");
                                                               } else {
                                                                   emit_inst("movq", "%rax, (%rcx)");
                                                               }
                                                           } else {
                                                               if (target_is_float) {
                                                                   emit_inst("movsd", std::format("%xmm0, {}(%rbp)", it->second.stack_offset));
                                                               } else {
                                                                   emit_inst("movq", std::format("%rax, {}(%rbp)", it->second.stack_offset));
                                                               }
                                                           }
                                                       }
                                                   } else if (auto* call_expr = std::get_if<ast::CallOrIndexExpr>(&s.target->data)) {
                                                       auto it = symbol_table_.find(to_lower(call_expr->callee));
                                                       bool target_is_float = (it != symbol_table_.end() && (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat));

                                                       if (target_is_float && !rhs_is_float) {
                                                           emit_inst("cvtsi2sdq", "%rax, %xmm0");
                                                       } else if (!target_is_float && rhs_is_float) {
                                                           emit_inst("cvttsd2siq", "%xmm0, %rax");
                                                       }

                                                       int temp_offset = allocate_stack_space(8);
                                                       if (target_is_float) {
                                                           emit_inst("movsd", std::format("%xmm0, {}(%rbp)", temp_offset));
                                                       } else {
                                                           emit_inst("movq", std::format("%rax, {}(%rbp)", temp_offset));
                                                       }

                                                       if (it != symbol_table_.end()) {
                                                           gen_array_element_address(it->second, call_expr->args); // -> %rax = element address

                                                           if (target_is_float) {
                                                               emit_inst("movq", "%rax, %rcx");
                                                               emit_inst("movsd", std::format("{}(%rbp), %xmm0", temp_offset));
                                                               emit_inst("movsd", "%xmm0, (%rcx)");
                                                           } else {
                                                               emit_inst("movq", "%rax, %rcx");
                                                               emit_inst("movq", std::format("{}(%rbp), %rdx", temp_offset));
                                                               emit_inst("movq", "%rdx, (%rcx)");
                                                           }
                                                       }
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::IfStmt>) {
                                                   std::string else_label = get_label(".L_else_");
                                                   std::string end_label = get_label(".L_if_end_");

                                                   gen_expr(s.condition);
                                                   emit_inst("cmpq", "$0, %rax");
                                                   emit_inst("je", s.else_branch.empty() ? end_label : else_label);

                                                   for (const auto& st : s.then_branch) gen_stmt(st);

                                                   if (!s.else_branch.empty()) {
                                                       emit_inst("jmp", end_label);
                                                       emit_label(else_label);
                                                       for (const auto& st : s.else_branch) gen_stmt(st);
                                                   }

                                                   emit_label(end_label);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::DoGroupStmt>) {
                                                   for (const auto& st : s.body) gen_stmt(st);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::DoWhileStmt>) {
                                                   std::string loop_start = get_label(".L_while_start_");
                                                   std::string loop_end = get_label(".L_while_end_");

                                                   emit_label(loop_start);
                                                   gen_expr(s.condition);
                                                   emit_inst("cmpq", "$0, %rax");
                                                   emit_inst("je", loop_end);

                                                   for (const auto& st : s.body) gen_stmt(st);

                                                   emit_inst("jmp", loop_start);
                                                   emit_label(loop_end);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::DoUntilStmt>) {
                                                   // PL/I DO UNTIL tests after the body (like do..while in C):
                                                   // the body always runs at least once, then loops while the
                                                   // condition is still false.
                                                   std::string loop_start = get_label(".L_until_start_");
                                                   std::string loop_end = get_label(".L_until_end_");

                                                   emit_label(loop_start);
                                                   for (const auto& st : s.body) gen_stmt(st);

                                                   gen_expr(s.condition);
                                                   emit_inst("cmpq", "$0, %rax");
                                                   emit_inst("je", loop_start);
                                                   emit_label(loop_end);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::DoIterativeStmt>) {
                                                   std::string loop_start = get_label(".L_loop_start_");
                                                   std::string loop_end = get_label(".L_loop_end_");

                                                   gen_expr(s.start);
                                                   auto it = symbol_table_.find(to_lower(s.var_name));
                                                   int var_offset = (it != symbol_table_.end()) ? it->second.stack_offset : allocate_stack_space(8);
                                                   emit_inst("movq", std::format("%rax, {}(%rbp)", var_offset));

                                                   emit_label(loop_start);

                                                   emit_inst("movq", std::format("{}(%rbp), %rax", var_offset));
                                                   emit_inst("pushq", "%rax");
                                                   gen_expr(s.to);
                                                   emit_inst("popq", "%rcx");
                                                   emit_inst("cmpq", "%rax, %rcx");
                                                   emit_inst("jg", loop_end);

                                                   for (const auto& st : s.body) gen_stmt(st);

                                                   if (s.by) gen_expr(s.by);
                                                   else emit_inst("movq", "$1, %rax");

                                                   emit_inst("addq", std::format("%rax, {}(%rbp)", var_offset));
                                                   emit_inst("jmp", loop_start);

                                                   emit_label(loop_end);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::SelectStmt>) {
                                                   std::string end_select = get_label(".L_select_end_");

                                                   bool has_select_expr = static_cast<bool>(s.expr);
                                                   bool select_is_float = false;
                                                   int select_offset = 0;
                                                   if (has_select_expr) {
                                                       select_is_float = gen_expr(s.expr);
                                                       select_offset = allocate_stack_space(8);
                                                       emit_inst(select_is_float ? "movsd" : "movq",
                                                                 std::format("{}, {}(%rbp)", select_is_float ? "%xmm0" : "%rax", select_offset));
                                                   }

                                                   for (const auto& [conds, stmts] : s.when_clauses) {
                                                       std::string match_label = get_label(".L_when_match_");
                                                       std::string next_when = get_label(".L_next_when_");

                                                       // A WHEN matches if ANY of its comma-separated conditions
                                                       // matches (either "is truthy", for bare SELECT, or "equals
                                                       // the select expression").
                                                       for (const auto& cond : conds) {
                                                           gen_when_condition(has_select_expr, select_is_float, select_offset, cond, match_label);
                                                       }
                                                       emit_inst("jmp", next_when);

                                                       emit_label(match_label);
                                                       for (const auto& st : stmts) gen_stmt(st);
                                                       emit_inst("jmp", end_select);

                                                       emit_label(next_when);
                                                   }

                                                   for (const auto& st : s.otherwise_branch) gen_stmt(st);
                                                   emit_label(end_select);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::CallStmt>) {
                                                   static const char* arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
                                                   for (size_t i = 0; i < s.args.size() && i < 6; ++i) {
                                                       if (auto* call_expr = std::get_if<ast::CallOrIndexExpr>(&s.args[i]->data)) {
                                                           auto it = symbol_table_.find(to_lower(call_expr->callee));
                                                           if (it != symbol_table_.end()) {
                                                               // Array element, passed by reference (PL/I default).
                                                               gen_array_element_address(it->second, call_expr->args); // -> %rax = element address
                                                               emit_inst("movq", std::format("%rax, {}", arg_regs[i]));
                                                           } else {
                                                               // Not a known variable, so this is a genuine nested
                                                               // call used as an argument, e.g. CALL FOO(BAR(X)) —
                                                               // evaluate it and pass a reference to the result,
                                                               // same as any other expression argument below.
                                                               bool is_float = gen_expr(s.args[i]);
                                                               int temp_offset = allocate_stack_space(8);
                                                               emit_inst(is_float ? "movsd" : "movq",
                                                                         std::format("{}, {}(%rbp)", is_float ? "%xmm0" : "%rax", temp_offset));
                                                               emit_inst("leaq", std::format("{}(%rbp), %rax", temp_offset));
                                                               emit_inst("movq", std::format("%rax, {}", arg_regs[i]));
                                                           }
                                                       } else if (auto* var_expr = std::get_if<ast::VarExpr>(&s.args[i]->data)) {
                                                           auto it = symbol_table_.find(to_lower(var_expr->name));
                                                           if (it != symbol_table_.end()) {
                                                               emit_inst("leaq", std::format("{}(%rbp), %rax", it->second.stack_offset));
                                                               emit_inst("movq", std::format("%rax, {}", arg_regs[i]));
                                                           }
                                                       } else {
                                                           bool is_float = gen_expr(s.args[i]);
                                                           int temp_offset = allocate_stack_space(8);
                                                           if (is_float) {
                                                               emit_inst("movsd", std::format("%xmm0, {}(%rbp)", temp_offset));
                                                           } else {
                                                               emit_inst("movq", std::format("%rax, {}(%rbp)", temp_offset));
                                                           }
                                                           emit_inst("leaq", std::format("{}(%rbp), %rax", temp_offset));
                                                           emit_inst("movq", std::format("%rax, {}", arg_regs[i]));
                                                       }
                                                   }
                                                   std::string callee = std::format("pli_{}", s.callee);
                                                   std::transform(callee.begin(), callee.end(), callee.begin(), ::tolower);
                                                   emit_inst("movq", "$0, %rax");
                                                   emit_inst("call", callee);
                                               }
                                               else if constexpr (std::is_same_v<T, ast::PutStmt>) {
                                                   if (s.skip) {
                                                       emit_inst("leaq", ".LC_newline(%rip), %rdi");
                                                       emit_inst("movq", "$0, %rax");
                                                       emit_inst("call", "printf@PLT");
                                                   }

                                                   for (const auto& item : s.items) {
                                                       bool is_str = false;

                                                       if (auto* var = std::get_if<ast::VarExpr>(&item->data)) {
                                                           auto it = symbol_table_.find(to_lower(var->name));
                                                           if (it != symbol_table_.end() && it->second.kind == TypeKind::String) {
                                                               is_str = true;
                                                           }
                                                       } else if (auto* lit = std::get_if<ast::LiteralExpr>(&item->data); lit && lit->lit_type == ast::LiteralExpr::Type::String) {
                                                           is_str = true;
                                                       }

                                                       bool is_float = gen_expr(item);

                                                       if (is_str) {
                                                           emit_inst("movq", "%rax, %rsi");
                                                           emit_inst("leaq", ".LC_fmt_str(%rip), %rdi");
                                                           emit_inst("movq", "$0, %rax");
                                                       } else if (is_float) {
                                                           emit_inst("leaq", ".LC_fmt_float(%rip), %rdi");
                                                           emit_inst("movq", "$1, %rax");
                                                       } else {
                                                           emit_inst("movq", "%rax, %rsi");
                                                           emit_inst("leaq", ".LC_fmt_int(%rip), %rdi");
                                                           emit_inst("movq", "$0, %rax");
                                                       }

                                                       emit_inst("call", "printf@PLT");
                                                   }

                                                   emit_inst("leaq", ".LC_newline(%rip), %rdi");
                                                   emit_inst("movq", "$0, %rax");
                                                   emit_inst("call", "printf@PLT");
                                               }
                                               else if constexpr (std::is_same_v<T, ast::GetStmt>) {
                                                   for (const auto& item : s.items) {
                                                       if (auto* var = std::get_if<ast::VarExpr>(&item->data)) {
                                                           auto it = symbol_table_.find(to_lower(var->name));
                                                           if (it == symbol_table_.end()) continue; // unknown target; nothing safe to do

                                                           bool is_ref = (it->second.kind == TypeKind::RefInt || it->second.kind == TypeKind::RefFloat);
                                                           int offset = it->second.stack_offset;

                                                           if (it->second.kind == TypeKind::String) {
                                                               // Strings are plain pointers with no owned buffer of
                                                               // their own — allocate a fresh one to scan into,
                                                               // then point the variable at it.
                                                               emit_inst("movq", "$256, %rdi");
                                                               emit_inst("call", "malloc@PLT");
                                                               emit_inst("movq", "%rax, %r12");
                                                               emit_inst("movq", "%rax, %rsi");
                                                               emit_inst("leaq", ".LC_scan_str(%rip), %rdi");
                                                               emit_inst("movq", "$0, %rax");
                                                               emit_inst("call", "scanf@PLT");
                                                               if (is_ref) {
                                                                   emit_inst("movq", std::format("{}(%rbp), %rcx", offset));
                                                                   emit_inst("movq", "%r12, (%rcx)");
                                                               } else {
                                                                   emit_inst("movq", std::format("%r12, {}(%rbp)", offset));
                                                               }
                                                               continue;
                                                           }

                                                           bool is_float = (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
                                                           if (is_ref) {
                                                               emit_inst("movq", std::format("{}(%rbp), %rsi", offset));
                                                           } else {
                                                               emit_inst("leaq", std::format("{}(%rbp), %rsi", offset));
                                                           }
                                                           emit_inst("leaq", is_float ? ".LC_scan_float(%rip), %rdi" : ".LC_scan_int(%rip), %rdi");
                                                           emit_inst("movq", "$0, %rax");
                                                           emit_inst("call", "scanf@PLT");
                                                       } else if (auto* call_expr = std::get_if<ast::CallOrIndexExpr>(&item->data)) {
                                                           auto it = symbol_table_.find(to_lower(call_expr->callee));
                                                           if (it == symbol_table_.end()) continue;

                                                           gen_array_element_address(it->second, call_expr->args); // -> %rax = element address
                                                           emit_inst("movq", "%rax, %rsi");
                                                           bool is_float = (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
                                                           emit_inst("leaq", is_float ? ".LC_scan_float(%rip), %rdi" : ".LC_scan_int(%rip), %rdi");
                                                           emit_inst("movq", "$0, %rax");
                                                           emit_inst("call", "scanf@PLT");
                                                       }
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
                                                   if (s.value) gen_expr(s.value);
                                                   emit_inst("movq", "%rbp, %rsp");
                                                   emit_inst("popq", "%rbp");
                                                   emit_inst("ret");
                                               }
                                               else if constexpr (std::is_same_v<T, ast::StopStmt>) {
                                                   emit_inst("movq", "$0, %rdi");
                                                   emit_inst("call", "exit@PLT");
                                               }
                                           }, stmt->data);
                                       }

                                       bool CodeGenerator::gen_expr(const ast::ExprPtr& expr) {
                                           if (!expr) {
                                               emit_inst("movq", "$0, %rax");
                                               return false;
                                           }

                                           return std::visit([this](auto&& e) -> bool {
                                               using T = std::decay_t<decltype(e)>;

                                               if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
                                                   if (e.lit_type == ast::LiteralExpr::Type::Int) {
                                                       emit_inst("movq", std::format("${}, %rax", e.value));
                                                       return false;
                                                   } else if (e.lit_type == ast::LiteralExpr::Type::Float) {
                                                       double val = std::atof(e.value.c_str());
                                                       std::string lbl = add_double_literal(val);
                                                       emit_inst("movsd", std::format("{}(%rip), %xmm0", lbl));
                                                       return true;
                                                   } else if (e.lit_type == ast::LiteralExpr::Type::String) {
                                                       std::string lbl = add_string_literal(e.value);
                                                       emit_inst("leaq", std::format("{}(%rip), %rax", lbl));
                                                       return false;
                                                   } else {
                                                       emit_inst("movq", "$0, %rax");
                                                       return false;
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::VarExpr>) {
                                                   auto it = symbol_table_.find(to_lower(e.name));
                                                   if (it != symbol_table_.end()) {
                                                       bool is_float = (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat);
                                                       bool is_ref = (it->second.kind == TypeKind::RefInt || it->second.kind == TypeKind::RefFloat);

                                                       if (is_ref) {
                                                           emit_inst("movq", std::format("{}(%rbp), %rcx", it->second.stack_offset));
                                                           if (is_float) {
                                                               emit_inst("movsd", "(%rcx), %xmm0");
                                                               return true;
                                                           } else {
                                                               emit_inst("movq", "(%rcx), %rax");
                                                               return false;
                                                           }
                                                       } else {
                                                           if (is_float) {
                                                               emit_inst("movsd", std::format("{}(%rbp), %xmm0", it->second.stack_offset));
                                                               return true;
                                                           } else {
                                                               emit_inst("movq", std::format("{}(%rbp), %rax", it->second.stack_offset));
                                                               return false;
                                                           }
                                                       }
                                                   } else {
                                                       emit_inst("movq", "$0, %rax");
                                                       return false;
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
                                                   bool left_is_float = gen_expr(e.left);

                                                   int temp_offset = allocate_stack_space(8);
                                                   if (left_is_float) {
                                                       emit_inst("movsd", std::format("%xmm0, {}(%rbp)", temp_offset));
                                                   } else {
                                                       emit_inst("movq", std::format("%rax, {}(%rbp)", temp_offset));
                                                   }

                                                   bool right_is_float = gen_expr(e.right);

                                                   if (left_is_float || right_is_float) {
                                                       if (!right_is_float) {
                                                           emit_inst("cvtsi2sdq", "%rax, %xmm0");
                                                       }

                                                       if (left_is_float) {
                                                           emit_inst("movsd", std::format("{}(%rbp), %xmm1", temp_offset));
                                                       } else {
                                                           emit_inst("movq", std::format("{}(%rbp), %rax", temp_offset));
                                                           emit_inst("cvtsi2sdq", "%rax, %xmm1");
                                                       }

                                                       if (e.op == "+") {
                                                           emit_inst("addsd", "%xmm0, %xmm1");
                                                           emit_inst("movaps", "%xmm1, %xmm0");
                                                       } else if (e.op == "-") {
                                                           emit_inst("subsd", "%xmm0, %xmm1");
                                                           emit_inst("movaps", "%xmm1, %xmm0");
                                                       } else if (e.op == "*") {
                                                           emit_inst("mulsd", "%xmm0, %xmm1");
                                                           emit_inst("movaps", "%xmm1, %xmm0");
                                                       } else if (e.op == "/") {
                                                           emit_inst("divsd", "%xmm0, %xmm1");
                                                           emit_inst("movaps", "%xmm1, %xmm0");
                                                       } else if (e.op == "=" || e.op == ">=" || e.op == "<=" || e.op == ">" || e.op == "<") {
                                                           emit_inst("ucomisd", "%xmm0, %xmm1");
                                                           if (e.op == "=") emit_inst("sete", "%al");
                                                           else if (e.op == ">=") emit_inst("setae", "%al");
                                                           else if (e.op == "<=") emit_inst("setbe", "%al");
                                                           else if (e.op == ">") emit_inst("seta", "%al");
                                                           else if (e.op == "<") emit_inst("setb", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                           return false;
                                                       }
                                                       return true;
                                                   } else {
                                                       emit_inst("movq", std::format("{}(%rbp), %rcx", temp_offset));

                                                       if (e.op == "||") {
                                                           // String concatenation: %rcx = left ptr, %rax = right
                                                           // ptr. Neither operand's underlying buffer is mutated —
                                                           // consistent with treating strings as immutable
                                                           // value-semantics pointers everywhere else in this
                                                           // codegen — a fresh malloc'd buffer is built and
                                                           // returned instead. Only libc (strlen/malloc/strcpy/
                                                           // strcat) is used, so no extra runtime object needs to
                                                           // be linked in.
                                                           emit_inst("pushq", "%rbx");
                                                           emit_inst("pushq", "%r12");
                                                           emit_inst("pushq", "%r13");
                                                           emit_inst("movq", "%rax, %r13");   // r13 = right ptr
                                                           emit_inst("movq", "%rcx, %r12");   // r12 = left ptr

                                                           emit_inst("movq", "%r12, %rdi");
                                                           emit_inst("call", "strlen@PLT");
                                                           emit_inst("movq", "%rax, %rbx");   // rbx = len(left)

                                           emit_inst("movq", "%r13, %rdi");
                                           emit_inst("call", "strlen@PLT");
                                           emit_inst("addq", "%rbx, %rax");
                                           emit_inst("addq", "$1, %rax");     // + NUL terminator
                                           emit_inst("movq", "%rax, %rdi");
                                           emit_inst("call", "malloc@PLT");
                                           emit_inst("movq", "%rax, %rbx");   // rbx = new buffer

                                           emit_inst("movq", "%rbx, %rdi");
                                           emit_inst("movq", "%r12, %rsi");
                                           emit_inst("call", "strcpy@PLT");
                                           emit_inst("movq", "%rbx, %rdi");
                                           emit_inst("movq", "%r13, %rsi");
                                           emit_inst("call", "strcat@PLT");

                                           emit_inst("movq", "%rbx, %rax");   // result ptr
                                           emit_inst("popq", "%r13");
                                           emit_inst("popq", "%r12");
                                           emit_inst("popq", "%rbx");
                                           return false;
                                                       } else if (e.op == "&" || e.op == "|") {
                                                           // Both operands are boolean-valued ints (0/1 — the
                                                           // result of a comparison, or a 0/1 literal) in this
                                                           // compiler's model. Reduce each side to a strict 0/1
                                                           // boolean, then combine bitwise, which is equivalent to
                                                           // logical AND/OR once both sides are 0/1.
                                                           emit_inst("cmpq", "$0, %rax");
                                                           emit_inst("setne", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                           emit_inst("cmpq", "$0, %rcx");
                                                           emit_inst("setne", "%cl");
                                                           emit_inst("movzbq", "%cl, %rcx");
                                                           emit_inst(e.op == "&" ? "andq" : "orq", "%rcx, %rax");
                                                           return false;
                                                       } else if (e.op == "+") {
                                                           emit_inst("addq", "%rcx, %rax");
                                                       } else if (e.op == "-") {
                                                           emit_inst("subq", "%rax, %rcx");
                                                           emit_inst("movq", "%rcx, %rax");
                                                       } else if (e.op == "*") {
                                                           emit_inst("imulq", "%rcx, %rax");
                                                       } else if (e.op == "/") {
                                                           emit_inst("movq", "%rax, %rbx");
                                                           emit_inst("movq", "%rcx, %rax");
                                                           emit_inst("cqto");
                                                           emit_inst("idivq", "%rbx");
                                                       } else if (e.op == "=") {
                                                           emit_inst("cmpq", "%rax, %rcx");
                                                           emit_inst("sete", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                       } else if (e.op == ">=") {
                                                           emit_inst("cmpq", "%rax, %rcx");
                                                           emit_inst("setge", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                       } else if (e.op == "<=") {
                                                           emit_inst("cmpq", "%rax, %rcx");
                                                           emit_inst("setle", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                       } else if (e.op == ">") {
                                                           emit_inst("cmpq", "%rax, %rcx");
                                                           emit_inst("setg", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                       } else if (e.op == "<") {
                                                           emit_inst("cmpq", "%rax, %rcx");
                                                           emit_inst("setl", "%al");
                                                           emit_inst("movzbq", "%al, %rax");
                                                       }
                                                       return false;
                                                   }
                                               }
                                               else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
                                                   bool is_float = gen_expr(e.expr);
                                                   if (e.op == "-") {
                                                       if (is_float) {
                                                           std::string zero_lbl = add_double_literal(0.0);
                                                           emit_inst("movsd", std::format("{}(%rip), %xmm1", zero_lbl));
                                                           emit_inst("subsd", "%xmm0, %xmm1");
                                                           emit_inst("movaps", "%xmm1, %xmm0");
                                                           return true;
                                                       } else {
                                                           emit_inst("negq", "%rax");
                                                           return false;
                                                       }
                                                   } else if (e.op == "^" || e.op == "!") {
                                                       emit_inst("cmpq", "$0, %rax");
                                                       emit_inst("sete", "%al");
                                                       emit_inst("movzbq", "%al, %rax");
                                                       return false;
                                                   }
                                                   return is_float;
                                               }
                                               else if constexpr (std::is_same_v<T, ast::CallOrIndexExpr>) {
                                                   auto it = symbol_table_.find(to_lower(e.callee));
                                                   if (it != symbol_table_.end()) {
                                                       gen_array_element_address(it->second, e.args); // -> %rax = element address
                                                       if (it->second.kind == TypeKind::Float || it->second.kind == TypeKind::RefFloat) {
                                                           emit_inst("movsd", "(%rax), %xmm0");
                                                           return true;
                                                       } else {
                                                           emit_inst("movq", "(%rax), %rax");
                                                           return false;
                                                       }
                                                   } else {
                                                       std::string upper_callee = e.callee;
                                                       std::transform(upper_callee.begin(), upper_callee.end(), upper_callee.begin(), ::toupper);

                                                       if (upper_callee == "ABS" && !e.args.empty()) {
                                                           // Stays int-valued for an int argument — only the
                                                           // float case goes through libm's fabs.
                                                           bool arg_is_float = gen_expr(e.args[0]);
                                                           if (arg_is_float) {
                                                               emit_inst("call", "fabs@PLT");
                                                               return true;
                                                           } else {
                                                               std::string end_lbl = get_label(".L_abs_end_");
                                                               emit_inst("cmpq", "$0, %rax");
                                                               emit_inst("jns", end_lbl);
                                                               emit_inst("negq", "%rax");
                                                               emit_label(end_lbl);
                                                               return false;
                                                           }
                                                       }

                                                       if (const char* libm_fn = builtin_math_fn(e.callee); libm_fn && !e.args.empty()) {
                                                           // Plain C ABI: argument by value in %xmm0, result by
                                                           // value in %xmm0 — NOT by-reference like the PL/I
                                                           // procedures below.
                                                           bool arg_is_float = gen_expr(e.args[0]);
                                                           if (!arg_is_float) emit_inst("cvtsi2sdq", "%rax, %xmm0");
                                                           emit_inst("call", std::format("{}@PLT", libm_fn));
                                                           return true;
                                                       }

                                                       // User-defined PL/I procedure: parameters are BY
                                                       // REFERENCE by default (see gen_procedure), so every
                                                       // actual argument is passed as a pointer, never a bare
                                                       // value.
                                                       static const char* arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
                                                       for (size_t i = 0; i < e.args.size() && i < 6; ++i) {
                                                           gen_arg_address(e.args[i]);
                                                           emit_inst("movq", std::format("%rax, {}", arg_regs[i]));
                                                       }
                                                       std::string callee_lower = to_lower(e.callee);
                                                       std::string callee = std::format("pli_{}", callee_lower);
                                                       emit_inst("movq", "$0, %rax");
                                                       emit_inst("call", callee);

                                                       auto rit = proc_returns_float_.find(callee_lower);
                                                       return rit != proc_returns_float_.end() && rit->second;
                                                   }
                                               }
                                               return false;
                                           }, expr->data);
                                       }

} // namespace xpln
