#include <iostream>
#include <string>
#include <string_view>

import token;
import lexer;
import ast;
import parser;
import logger;
import semantic;
import semantic.analyzer;
import codegen;
import driver;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

namespace {
	bool compile_to_ir_with_opt(std::string_view code, OptLevel level, std::string &out_ir) {
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		if (p.has_errors()) {
			std::cerr << "Parser error in test code\n";
			return false;
		}

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		if (diag.has_errors()) {
			diag.print_all(std::cerr);
			return false;
		}

		CodeGen cg{&sema, "test_opt_module"};
		if (!cg.generate(prog)) {
			std::cerr << "CodeGen LLVM verification failed!\n";
			return false;
		}

		if (!cg.optimize(level)) {
			std::cerr << "CodeGen optimization pass failed!\n";
			return false;
		}

		out_ir = cg.dump_ir();
		return true;
	}

	bool test_mem2reg_and_constant_folding() {
		std::string_view code =
			"fn compute(x: i32): i32 {\n"
			"    var a = x;\n"
			"    var b = 10;\n"
			"    var c = 20;\n"
			"    return a + b * c;\n"
			"}\n";

		std::string ir_o0, ir_o2;
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O0, ir_o0), "O0 compilation failed");
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O2, ir_o2), "O2 compilation failed");

		// At O0, alloca exists for local vars
		ASSERT(ir_o0.find("alloca") != std::string::npos, "O0 should retain alloca instructions");

		// At O2, Mem2Reg eliminates alloca, and 10 * 20 is folded into 200
		ASSERT(ir_o2.find("alloca") == std::string::npos, "O2 should eliminate alloca via Mem2Reg");
		ASSERT(ir_o2.find("200") != std::string::npos, "O2 should constant fold 10 * 20 into 200");
		return true;
	}

	bool test_function_inlining() {
		std::string_view code =
			"fn square(x: i32): i32 {\n"
			"    return x * x;\n"
			"}\n"
			"fn sum_squares(a: i32, b: i32): i32 {\n"
			"    return square(a) + square(b);\n"
			"}\n";

		std::string ir_o0, ir_o2;
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O0, ir_o0), "O0 compilation failed");
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O2, ir_o2), "O2 compilation failed");

		// At O0, sum_squares calls square
		ASSERT(ir_o0.find("call i32 @square") != std::string::npos, "O0 should retain function calls");

		// At O2, square is small and should be inlined into sum_squares
		auto sum_pos = ir_o2.find("define i32 @sum_squares");
		ASSERT(sum_pos != std::string::npos, "sum_squares not found in O2 IR");
		auto body_after = ir_o2.substr(sum_pos);
		auto end_fn = body_after.find('}');
		ASSERT(end_fn != std::string::npos, "closing brace of sum_squares not found");
		std::string sum_body = body_after.substr(0, end_fn);

		ASSERT(sum_body.find("call i32 @square") == std::string::npos, "O2 should inline square call into sum_squares");
		ASSERT(sum_body.find("mul i32") != std::string::npos, "O2 should contain inlined multiplication");
		return true;
	}

	bool test_dead_code_elimination() {
		std::string_view code =
			"fn dce_test(x: i32): i32 {\n"
			"    var unused = 99999;\n"
			"    var dead_calc = x * 777;\n"
			"    return x;\n"
			"}\n";

		std::string ir_o0, ir_o2;
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O0, ir_o0), "O0 compilation failed");
		ASSERT(compile_to_ir_with_opt(code, OptLevel::O2, ir_o2), "O2 compilation failed");

		// At O0, 99999 and 777 exist
		ASSERT(ir_o0.find("99999") != std::string::npos, "O0 should contain 99999");
		ASSERT(ir_o0.find("777") != std::string::npos, "O0 should contain 777");

		// At O2, dead variables and unused calculations are eliminated
		ASSERT(ir_o2.find("99999") == std::string::npos, "O2 should eliminate unused constant 99999");
		ASSERT(ir_o2.find("777") == std::string::npos, "O2 should eliminate dead calculation 777");
		return true;
	}

	bool test_all_opt_levels() {
		std::string_view code =
			"fn fib(n: i32): i32 {\n"
			"    if (n <= 1) { return n; }\n"
			"    return fib(n - 1) + fib(n - 2);\n"
			"}\n";

		const OptLevel levels[] = {
			OptLevel::O0,
			OptLevel::O1,
			OptLevel::O2,
			OptLevel::O3,
			OptLevel::Os,
			OptLevel::Oz
		};

		for (auto lvl : levels) {
			std::string ir;
			ASSERT(compile_to_ir_with_opt(code, lvl, ir), "Optimization failed for level");
			ASSERT(ir.find("define i32 @fib(") != std::string::npos, "Missing fib function");
		}
		return true;
	}

	bool test_driver_options() {
		CompilerOptions opts;
		ASSERT(opts.opt_level == OptLevel::O0, "Default opt level should be O0");

		opts.opt_level = OptLevel::O3;
		ASSERT(opts.opt_level == OptLevel::O3, "Setting opt level to O3 failed");
		return true;
	}
}

int main() {
	std::cout << "[RUNNING] Optimization Pipeline Tests...\n";

	if (!test_driver_options()) return 1;
	if (!test_mem2reg_and_constant_folding()) return 1;
	if (!test_function_inlining()) return 1;
	if (!test_dead_code_elimination()) return 1;
	if (!test_all_opt_levels()) return 1;

	std::cout << "[PASSED] All Optimization Pipeline Tests passed successfully!\n";
	return 0;
}
