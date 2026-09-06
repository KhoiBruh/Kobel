#include <iostream>
#include <string>
#include <string_view>

import lexer;
import parser;
import semantic;
import semantic.analyzer;
import codegen;
import logger;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

static bool compile_to_ir(std::string_view code, std::string& out_ir) {
	Lexer lex{code};
	Parser parser{lex.tokenize()};
	auto prog = parser.parse_program();
	if (parser.has_errors()) return false;

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	if (diag.has_errors()) return false;

	CodeGen cg{&sema, "test_module"};
	if (!cg.generate(prog.get())) return false;

	out_ir = cg.dump_ir();
	return true;
}

// 1. Test variable shadowing in CodeGen: outer variable is restored after inner block
bool test_variable_shadowing_codegen() {
	std::string_view code =
		"fn test_shadow(): i32 {\n"
		"    var x: i32 = 10;\n"
		"    {\n"
		"        var x: i32 = 20;\n"
		"    }\n"
		"    return x;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Compilation of variable shadowing failed");
	// Should have multiple allocas for x
	size_t first_alloca = ir.find("alloca i32");
	ASSERT(first_alloca != std::string::npos, "Missing first alloca for x");
	size_t second_alloca = ir.find("alloca i32", first_alloca + 1);
	ASSERT(second_alloca != std::string::npos, "Missing second (shadowed) alloca for x");
	return true;
}

// 2. Test pointer mutability soundness: *T vs &T
bool test_pointer_mutability_soundness() {
	// 2a. &T can be parsed and assigned to *T (safe decay)
	{
		std::string_view code =
			"fn test_decay(p_mut: &i32): void {\n"
			"    val p_const: *i32 = p_mut;\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();
		ASSERT(!parser.has_errors(), "Parser failed on &T pointer syntax");

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(!diag.has_errors(), "Expected &T to *T assignment to succeed");
	}

	// 2b. *T cannot be assigned to &T (loss of const safety)
	{
		std::string_view code =
			"fn test_invalid(p_const: *i32): void {\n"
			"    val p_mut: &i32 = p_const;\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Expected *T to &T assignment to be rejected");
	}

	return true;
}

// 3. Test string literal escape quotes and unterminated string
bool test_string_literal_escapes() {
	// 3a. String containing escaped quote \"
	{
		std::string_view code = "val msg = \"Hello \\\"World\\\"\";";
		Lexer lex{code};
		auto tokens = lex.tokenize();
		ASSERT(tokens.size() >= 5, "Token count mismatch for escaped string");
		ASSERT(tokens[3].type == TokenType::STRING, "Expected STRING token");
		ASSERT(tokens[3].text == "\"Hello \\\"World\\\"\"", "String token content mismatch");
	}

	// 3b. Unterminated string reaching EOF
	{
		std::string_view code =
			"fn test(): void {\n"
			"    val msg: *char = \"unclosed string;\n"
			"}\n";
		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();
		ASSERT(parser.has_errors(), "Parser should report error for unclosed string");
		bool found_unterminated = false;
		for (const auto& err : parser.errors) {
			if (err.find("Unterminated string literal") != std::string::npos) {
				found_unterminated = true;
				break;
			}
		}
		ASSERT(found_unterminated, "Missing 'Unterminated string literal' error message");
	}

	return true;
}

// 4. Test Definite Return analysis
bool test_definite_return_analysis() {
	// 4a. Missing return in simple non-void function
	{
		std::string_view code =
			"fn missing_ret(): i32 {\n"
			"    val a: i32 = 1;\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Analyzer must report missing return statement");
	}

	// 4b. Missing return in if without else
	{
		std::string_view code =
			"fn missing_in_if(x: i32): i32 {\n"
			"    if x > 0 {\n"
			"        return 1;\n"
			"    }\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Analyzer must report missing return when if lacks else");
	}

	// 4c. Valid return in both if and else
	{
		std::string_view code =
			"fn valid_if_else(x: i32): i32 {\n"
			"    if x > 0 {\n"
			"        return 1;\n"
			"    } else {\n"
			"        return 2;\n"
			"    }\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(!diag.has_errors(), "Valid if-else return must pass");
	}

	// 4d. Void function does not require return
	{
		std::string_view code =
			"fn do_something(): void {\n"
			"    val a: i32 = 1;\n"
			"}\n";

		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog.get());
		ASSERT(!diag.has_errors(), "Void function should not require return");
	}

	return true;
}

// 5. Test contextual integer literal typing
bool test_contextual_integer_literal_typing() {
	std::string_view code =
		"fn test_int_types(): i64 {\n"
		"    val a: i64 = 100;\n"
		"    val b: i16 = 5;\n"
		"    val c: i8 = 2;\n"
		"    return a;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Compilation of contextual integer literals failed");
	ASSERT(ir.find("store i64 100") != std::string::npos, "Expected store i64 100");
	ASSERT(ir.find("store i16 5") != std::string::npos, "Expected store i16 5");
	ASSERT(ir.find("store i8 2") != std::string::npos, "Expected store i8 2");
	return true;
}

int main() {
	std::cout << "[RUNNING] Latent Fixes & Soundness Tests..." << std::endl;

	if (!test_variable_shadowing_codegen()) return 1;
	std::cout << "  [PASS] test_variable_shadowing_codegen" << std::endl;

	if (!test_pointer_mutability_soundness()) return 1;
	std::cout << "  [PASS] test_pointer_mutability_soundness" << std::endl;

	if (!test_string_literal_escapes()) return 1;
	std::cout << "  [PASS] test_string_literal_escapes" << std::endl;

	if (!test_definite_return_analysis()) return 1;
	std::cout << "  [PASS] test_definite_return_analysis" << std::endl;

	if (!test_contextual_integer_literal_typing()) return 1;
	std::cout << "  [PASS] test_contextual_integer_literal_typing" << std::endl;

	std::cout << "[ALL PASSED] Latent Fixes Tests passed successfully!" << std::endl;
	return 0;
}
