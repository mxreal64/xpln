#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <print>
#include <sstream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println(std::cerr, "Usage: xpln <source.pli>");
        return 1;
    }

    std::string source_path = argv[1];
    std::ifstream file(source_path);
    if (!file) {
        std::println(std::cerr, "Error: Could not open source file {}", source_path);
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string src = buffer.str();

    try {
        xpln::Lexer lexer(src);
        auto lex_result = lexer.tokenize();

        xpln::Parser parser(lex_result.tokens);
        auto ast_tree = parser.parse();

        xpln::CodeGenerator cg;
        std::string compiled_asm = cg.generate(ast_tree);

        std::println("{}", compiled_asm);
    } catch (const xpln::LexerError& le) {
        std::println(std::cerr, "[Lexer Error] {}", le.what());
        return 2;
    } catch (const xpln::ParserError& pe) {
        std::println(std::cerr, "[Parser Error] {}", pe.what());
        return 3;
    } catch (const std::exception& e) {
        std::println(std::cerr, "[Internal Error] {}", e.what());
        return 4;
    }

    return 0;
}
