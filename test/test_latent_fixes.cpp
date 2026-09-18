#include <iostream>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <sstream>

import lexer;
import parser;
import semantic;
import semantic.analyzer;
import codegen;
import logger;
import driver;

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
	sema.analyze(prog);
	if (diag.has_errors()) return false;

	CodeGen cg{&sema, "test_module"};
	if (!cg.generate(prog)) return false;

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
		sema.analyze(prog);
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
		sema.analyze(prog);
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
		sema.analyze(prog);
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
		sema.analyze(prog);
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
		sema.analyze(prog);
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
		sema.analyze(prog);
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

// 6. Test Driver dependency resolution parser/arena lifetime & no use-after-free
bool test_driver_dependency_lifetime() {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path temp_dir = fs::temp_directory_path() / "kobel_test_dep_lifetime";
	fs::remove_all(temp_dir, ec);
	fs::create_directories(temp_dir, ec);

	// Case 1: Chained dependencies (A -> B -> C -> D)
	{
		std::ofstream(temp_dir / "mod_d.kb") << "mod mod_d;\npub fn calc_d(): i32 { return 10; }\n";
		std::ofstream(temp_dir / "mod_c.kb") << "mod mod_c;\nuse mod_d.calc_d;\npub fn calc_c(): i32 { return calc_d() + 1; }\n";
		std::ofstream(temp_dir / "mod_b.kb") << "mod mod_b;\nuse mod_c.calc_c;\npub fn calc_b(): i32 { return calc_c() + 2; }\n";
		std::ofstream(temp_dir / "mod_a.kb") << "mod mod_a;\nuse mod_b.calc_b;\npub fn calc_a(): i32 { return calc_b() + 3; }\n";
		std::ofstream(temp_dir / "main.kb") << "mod main;\nuse mod_a.calc_a;\nfn main(): i32 { return calc_a(); }\n";

		CompilerOptions opts;
		opts.input_files = { (temp_dir / "main.kb").string() };
		opts.output_file = (temp_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = { temp_dir };

		std::ostringstream out_s, err_s;
		Driver driver{opts, out_s, err_s};
		int res = driver.run();
		ASSERT(res == 0, ("Driver run failed on chained dependencies: " + err_s.str()).c_str());

		std::ifstream ir_file(temp_dir / "output.ll");
		std::string ir_content((std::istreambuf_iterator<char>(ir_file)), std::istreambuf_iterator<char>());
		ASSERT(ir_content.find("calc_d") != std::string::npos, "Missing calc_d in IR");
		ASSERT(ir_content.find("calc_c") != std::string::npos, "Missing calc_c in IR");
		ASSERT(ir_content.find("calc_b") != std::string::npos, "Missing calc_b in IR");
		ASSERT(ir_content.find("calc_a") != std::string::npos, "Missing calc_a in IR");
	}

	// Case 2: Diamond dependencies (main -> left, right; left -> base; right -> base)
	{
		fs::path diamond_dir = temp_dir / "diamond";
		fs::create_directories(diamond_dir, ec);

		std::ofstream(diamond_dir / "base.kb") << "mod base;\npub fn base_val(): i32 { return 42; }\n";
		std::ofstream(diamond_dir / "left.kb") << "mod left;\nuse base.base_val;\npub fn left_val(): i32 { return base_val() + 1; }\n";
		std::ofstream(diamond_dir / "right.kb") << "mod right;\nuse base.base_val;\npub fn right_val(): i32 { return base_val() * 2; }\n";
		std::ofstream(diamond_dir / "main.kb") << "mod main;\nuse left.left_val;\nuse right.right_val;\nfn main(): i32 { return left_val() + right_val(); }\n";

		CompilerOptions opts;
		opts.input_files = { (diamond_dir / "main.kb").string() };
		opts.output_file = (diamond_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = { diamond_dir };

		std::ostringstream out_s, err_s;
		Driver driver{opts, out_s, err_s};
		int res = driver.run();
		ASSERT(res == 0, ("Driver run failed on diamond dependencies: " + err_s.str()).c_str());

		std::ifstream ir_file(diamond_dir / "output.ll");
		std::string ir_content((std::istreambuf_iterator<char>(ir_file)), std::istreambuf_iterator<char>());
		ASSERT(ir_content.find("base_val") != std::string::npos, "Missing base_val in IR");
		ASSERT(ir_content.find("left_val") != std::string::npos, "Missing left_val in IR");
		ASSERT(ir_content.find("right_val") != std::string::npos, "Missing right_val in IR");
	}

	// Case 3: Circular dependencies (mod_a <-> mod_b mutual imports)
	{
		fs::path circ_dir = temp_dir / "circ";
		fs::create_directories(circ_dir, ec);

		std::ofstream(circ_dir / "mod_a.kb") << "mod mod_a;\nuse mod_b.func_b;\npub fn func_a(): i32 { return 10; }\n";
		std::ofstream(circ_dir / "mod_b.kb") << "mod mod_b;\nuse mod_a.func_a;\npub fn func_b(): i32 { return func_a() + 5; }\n";
		std::ofstream(circ_dir / "main.kb") << "mod main;\nuse mod_a.func_a;\nuse mod_b.func_b;\nfn main(): i32 { return func_a() + func_b(); }\n";

		CompilerOptions opts;
		opts.input_files = { (circ_dir / "main.kb").string() };
		opts.output_file = (circ_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = { circ_dir };

		std::ostringstream out_s, err_s;
		Driver driver{opts, out_s, err_s};
		int res = driver.run();
		ASSERT(res == 0, ("Driver run failed on circular dependencies: " + err_s.str()).c_str());

		std::ifstream ir_file(circ_dir / "output.ll");
		std::string ir_content((std::istreambuf_iterator<char>(ir_file)), std::istreambuf_iterator<char>());
		ASSERT(ir_content.find("func_a") != std::string::npos, "Missing func_a in IR");
		ASSERT(ir_content.find("func_b") != std::string::npos, "Missing func_b in IR");
	}

	fs::remove_all(temp_dir, ec);
	return true;
}

// 7. Test struct member codegen does not pollute analyzer->structs map via operator[]
bool test_struct_member_no_map_pollution() {
	std::string_view code =
		"struct Point {\n"
		"    x: i32,\n"
		"    y: i32,\n"
		"    z: i32\n"
		"}\n"
		"struct Nested {\n"
		"    pt: Point,\n"
		"    tag: i32\n"
		"}\n"
		"fn test_members(n: &Nested): i32 {\n"
		"    return n.pt.z + n.tag;\n"
		"}\n";

	Lexer lex{code};
	Parser parser{lex.tokenize()};
	auto prog = parser.parse_program();
	ASSERT(!parser.has_errors(), "Parser failed on struct definitions");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic analysis failed");

	size_t initial_struct_count = sema.structs.size();
	ASSERT(!sema.structs.contains("NonExistentGhostStruct"), "Ghost struct should not exist initially");

	CodeGen cg{&sema, "test_struct_module"};
	ASSERT(cg.generate(prog), "CodeGen failed for struct members");

	// Verify that code generation did not inject any bogus empty structs into sema.structs
	ASSERT(sema.structs.size() == initial_struct_count, "Struct map size must remain strictly unchanged after CodeGen");
	ASSERT(!sema.structs.contains("NonExistentGhostStruct"), "Ghost struct must not be inserted");

	std::string ir = cg.dump_ir();
	// Check struct GEPs exist for pt (idx 0), tag (idx 1), and z (idx 2)
	ASSERT(ir.find("getelementptr inbounds") != std::string::npos, "Missing GEP in emitted IR");
	ASSERT(ir.find("ret i32") != std::string::npos, "Missing ret i32 in emitted IR");

	// Stress test: large struct with 25 fields
	{
		std::string stress_code = "struct Huge {\n";
		for (int i = 0; i < 25; ++i) {
			stress_code += "    f" + std::to_string(i) + ": i32" + (i == 24 ? "\n" : ",\n");
		}
		stress_code += "}\nfn test_huge(h: &Huge): i32 {\n    return h.f0 + h.f12 + h.f24;\n}\n";

		Lexer s_lex{stress_code};
		Parser s_parser{s_lex.tokenize()};
		auto s_prog = s_parser.parse_program();
		ASSERT(!s_parser.has_errors(), "Parser failed on huge struct");

		DiagnosticEngine s_diag;
		Analyzer s_sema{s_diag};
		s_sema.analyze(s_prog);
		ASSERT(!s_diag.has_errors(), "Semantic analysis failed on huge struct");

		size_t huge_init_count = s_sema.structs.size();
		CodeGen s_cg{&s_sema, "huge_module"};
		ASSERT(s_cg.generate(s_prog), "CodeGen failed for huge struct");
		ASSERT(s_sema.structs.size() == huge_init_count, "Huge struct CodeGen must not add empty structs");

		std::string huge_ir = s_cg.dump_ir();
		ASSERT(huge_ir.find(", i32 12") != std::string::npos, "Missing GEP index 12 for f12");
		ASSERT(huge_ir.find(", i32 24") != std::string::npos, "Missing GEP index 24 for f24");
	}

	// Member access on non-existent member or unresolvable struct does not crash
	{
		std::string bad_code =
			"struct Dummy { a: i32 }\n"
			"fn test_bad(d: &Dummy): i32 {\n"
			"    return d.a;\n"
			"}\n";
		Lexer b_lex{bad_code};
		Parser b_parser{b_lex.tokenize()};
		auto b_prog = b_parser.parse_program();
		DiagnosticEngine b_diag;
		Analyzer b_sema{b_diag};
		b_sema.analyze(b_prog);

		CodeGen b_cg{&b_sema, "bad_module"};
		ASSERT(b_cg.generate(b_prog), "Expected valid dummy codegen to succeed");

		// Attempting to emit lvalue or expr on invalid member expression returns nullptr safely without crashing
		MemberExpr fake_member{nullptr, "non_existent", 1, 1};
		ASSERT(b_cg.emit_lvalue(&fake_member) == nullptr, "emit_lvalue on invalid member must return nullptr");
		ASSERT(b_cg.emit_expr(&fake_member) == nullptr, "emit_expr on invalid member must return nullptr without crashing");
	}

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

	if (!test_driver_dependency_lifetime()) return 1;
	std::cout << "  [PASS] test_driver_dependency_lifetime" << std::endl;

	if (!test_struct_member_no_map_pollution()) return 1;
	std::cout << "  [PASS] test_struct_member_no_map_pollution" << std::endl;

	std::cout << "[ALL PASSED] Latent Fixes Tests passed successfully!" << std::endl;
	return 0;
}

