#!/usr/bin/env python3
import sys
import os
import argparse
import subprocess
from lexer import Lexer, LexerError
from parser import Parser, ParserError
from codegen import CodeGenerator

def main():
    parser = argparse.ArgumentParser(description="PLIC: Complete PL/I Compiler (with Reserved Keywords)")
    parser.add_argument("source", help="PL/I source file (.pli)")
    parser.add_argument("-o", "--output", help="Output file for compiled Python script")
    parser.add_argument("-r", "--run", action="store_true", help="Compile and execute the program immediately")
    parser.add_argument("--tokens", action="store_true", help="Dump tokenized stream")
    parser.add_argument("--ast", action="store_true", help="Dump AST representation")
    parser.add_argument("-v", "--version", action="version", version="PLIC 1.0.0 (PL/I Subset G Compiler)")

    args = parser.parse_args()

    if not os.path.exists(args.source):
        print(f"Error: Source file '{args.source}' not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.source, "r", encoding="utf-8") as f:
        src = f.read()

    try:
        lexer = Lexer(src)
        tokens = lexer.tokenize()

        if args.tokens:
            print("=== TOKEN STREAM ===")
            for t in tokens:
                print(f"  {t}")

        p = Parser(tokens)
        ast_tree = p.parse()

        if args.ast:
            print("=== AST REPRESENTATION ===")
            import pprint
            pprint.pprint(ast_tree)

        cg = CodeGenerator()
        compiled_py = cg.generate(ast_tree, standalone=True)

        out_path = args.output
        if not out_path and not args.run:
            base, _ = os.path.splitext(args.source)
            out_path = base + ".py"

        if out_path:
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(compiled_py)
            print(f"[PLIC] Successfully compiled '{args.source}' -> '{out_path}'")

        if args.run:
            if not out_path:
                temp_run_file = "/tmp/__plic_run.py"
                with open(temp_run_file, "w", encoding="utf-8") as f:
                    f.write(compiled_py)
                run_target = temp_run_file
            else:
                run_target = out_path

            subprocess.run([sys.executable, run_target])

    except LexerError as le:
        print(f"[Lexer Error] {le}", file=sys.stderr)
        sys.exit(2)
    except ParserError as pe:
        print(f"[Parser Error] {pe}", file=sys.stderr)
        sys.exit(3)
    except BrokenPipeError:
        pass
    except Exception as e:
        print(f"[Internal Error] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(4)

if __name__ == "__main__":
    main()
