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

static bool compile_to_ir(std::string_view code, std::string &out_ir) {
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
		for (const auto &err: parser.errors) {
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
		std::ofstream(temp_dir / "mod_c.kb") <<
				"mod mod_c;\nuse mod_d.calc_d;\npub fn calc_c(): i32 { return calc_d() + 1; }\n";
		std::ofstream(temp_dir / "mod_b.kb") <<
				"mod mod_b;\nuse mod_c.calc_c;\npub fn calc_b(): i32 { return calc_c() + 2; }\n";
		std::ofstream(temp_dir / "mod_a.kb") <<
				"mod mod_a;\nuse mod_b.calc_b;\npub fn calc_a(): i32 { return calc_b() + 3; }\n";
		std::ofstream(temp_dir / "main.kb") << "mod main;\nuse mod_a.calc_a;\nfn main(): i32 { return calc_a(); }\n";

		CompilerOptions opts;
		opts.input_files = {(temp_dir / "main.kb").string()};
		opts.output_file = (temp_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = {temp_dir};

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
		std::ofstream(diamond_dir / "left.kb") <<
				"mod left;\nuse base.base_val;\npub fn left_val(): i32 { return base_val() + 1; }\n";
		std::ofstream(diamond_dir / "right.kb") <<
				"mod right;\nuse base.base_val;\npub fn right_val(): i32 { return base_val() * 2; }\n";
		std::ofstream(diamond_dir / "main.kb") <<
				"mod main;\nuse left.left_val;\nuse right.right_val;\nfn main(): i32 { return left_val() + right_val(); }\n";

		CompilerOptions opts;
		opts.input_files = {(diamond_dir / "main.kb").string()};
		opts.output_file = (diamond_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = {diamond_dir};

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
		std::ofstream(circ_dir / "mod_b.kb") <<
				"mod mod_b;\nuse mod_a.func_a;\npub fn func_b(): i32 { return func_a() + 5; }\n";
		std::ofstream(circ_dir / "main.kb") <<
				"mod main;\nuse mod_a.func_a;\nuse mod_b.func_b;\nfn main(): i32 { return func_a() + func_b(); }\n";

		CompilerOptions opts;
		opts.input_files = {(circ_dir / "main.kb").string()};
		opts.output_file = (circ_dir / "output.ll").string();
		opts.mode = OutputMode::IR;
		opts.custom_search_dirs = {circ_dir};

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
		ASSERT(b_cg.emit_expr(&fake_member) == nullptr,
			   "emit_expr on invalid member must return nullptr without crashing");
	}

	return true;
}

// 8. Test Lexer comment skipping does not cause stack overflow on deep comment chains
bool test_lexer_comment_stack_overflow_stress() {
	// Case 1: 50,000 consecutive single-line comments
	{
		std::string deep_comments;
		deep_comments.reserve(50000 * 20 + 50);
		for (int i = 0; i < 50000; ++i) {
			deep_comments += "// comment line\n";
		}
		deep_comments += "val final_token = 42;";

		Lexer lex{deep_comments};
		auto tokens = lex.tokenize();
		ASSERT(!tokens.empty(), "Tokens should not be empty");
		ASSERT(tokens.size() >= 5, "Expected tokens for 'val final_token = 42;'");
		ASSERT(tokens[0].type == TokenType::KW_VAL, "Expected KW_VAL token as first non-comment token");
		ASSERT(tokens[1].text == "final_token", "Expected final_token identifier");
		ASSERT(tokens.back().type == TokenType::END_OF_FILE, "Expected EOF token");
	}

	// Case 2: 25,000 consecutive block comments
	{
		std::string block_comments;
		block_comments.reserve(25000 * 15 + 50);
		for (int i = 0; i < 25000; ++i) {
			block_comments += "/* block */ ";
		}
		block_comments += "fn foo(): void {}";

		Lexer lex{block_comments};
		auto tokens = lex.tokenize();
		ASSERT(tokens[0].type == TokenType::KW_FN, "Expected KW_FN token after block comments");
		ASSERT(tokens[1].text == "foo", "Expected foo identifier");
	}

	// Case 3: Alternating line comments, block comments, and division operators
	{
		std::string mixed = "100 / /* c1 */ 2 // line comment\n / 5 /* c2 */";
		Lexer lex{mixed};
		auto tokens = lex.tokenize();
		// Tokens: 100, SLASH, 2, SLASH, 5, EOF
		ASSERT(tokens.size() == 6, "Expected 6 tokens for mixed comments and division");
		ASSERT(tokens[0].type == TokenType::NUMBER && tokens[0].text == "100", "Token 0 mismatch");
		ASSERT(tokens[1].type == TokenType::SLASH, "Token 1 mismatch");
		ASSERT(tokens[2].type == TokenType::NUMBER && tokens[2].text == "2", "Token 2 mismatch");
		ASSERT(tokens[3].type == TokenType::SLASH, "Token 3 mismatch");
		ASSERT(tokens[4].type == TokenType::NUMBER && tokens[4].text == "5", "Token 4 mismatch");
		ASSERT(tokens[5].type == TokenType::END_OF_FILE, "Token 5 mismatch");
	}

	// Case 4: Unclosed block comment at EOF
	{
		std::string unclosed = "/* unterminated block comment at eof";
		Lexer lex{unclosed};
		auto tokens = lex.tokenize();
		ASSERT(tokens.size() == 1, "Expected only EOF token for unclosed block comment");
		ASSERT(tokens[0].type == TokenType::END_OF_FILE, "Expected EOF token");
	}

	// Case 5: Carriage return line comment endings and CRLF/CR in block comments
	{
		std::string cr_comments =
				"// first comment\rval x = 1;\r// second comment\r\nval y = 2;\n/* block \r with cr \r\n and crlf */\nval z = 3;";
		Lexer lex{cr_comments};
		auto tokens = lex.tokenize();
		// Tokens for: val x = 1 ; val y = 2 ; val z = 3 ; EOF
		ASSERT(tokens.size() >= 15, "Expected tokens for x, y, and z declarations");
		ASSERT(tokens[0].type == TokenType::KW_VAL, "Expected KW_VAL for x");
		ASSERT(tokens[1].text == "x", "Expected identifier x");
		ASSERT(tokens[5].type == TokenType::KW_VAL, "Expected KW_VAL for y");
		ASSERT(tokens[6].text == "y", "Expected identifier y");
		ASSERT(tokens[10].type == TokenType::KW_VAL, "Expected KW_VAL for z");
		ASSERT(tokens[11].text == "z", "Expected identifier z");
		ASSERT(tokens[10].line == 8, ("Expected line 8 for z, got line " + std::to_string(tokens[10].line)).c_str());
	}

	return true;
}

// 9. Test multi-file module scope isolation
bool test_multifile_module_isolation() {
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "kobel_iso_test";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	std::filesystem::path f1 = dir / "f1.kb";
	std::filesystem::path f2 = dir / "f2.kb";

	{
		std::ofstream out(f1);
		out << "mod alpha;\n"
				<< "pub fn a_val(): i32 { return 10; }\n";
	}
	{
		std::ofstream out(f2);
		out << "fn root_helper(): i32 { return 20; }\n"
				<< "mod beta;\n"
				<< "use alpha.a_val;\n"
				<< "pub fn run(): i32 { return a_val() + root_helper(); }\n";
	}

	CompilerOptions opts;
	opts.input_files = {f1.string(), f2.string()};
	opts.mode = OutputMode::IR;
	opts.output_file = (dir / "out.ll").string();

	std::ostringstream err_out;
	Driver driver(opts, std::cout, err_out);
	int ret = driver.run();

	std::filesystem::remove_all(dir, ec);

	ASSERT(ret == 0, ("Multi-file module isolation failed: " + err_out.str()).c_str());
	return true;
}

bool test_array_flyweight_immutability() {
	std::string_view code =
			"fn test_arrays(): i32 {\n"
			"    val a: Array<i32> = [1, 2];\n"
			"    val b: Array<i32> = [10, 20, 30, 40];\n"
			"    return a[0] + b[0];\n"
			"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Array flyweight immutability compilation failed");
	ASSERT(ir.find("[2 x i32]") != std::string::npos, "Array a should have type [2 x i32]");
	ASSERT(ir.find("[4 x i32]") != std::string::npos, "Array b should have type [4 x i32]");
	return true;
}

bool test_ast_immutability_struct_and_impl() {
	std::string_view code =
			"struct Widget {\n"
			"    pub id: i32\n"
			"}\n"
			"impl Widget {\n"
			"    fn get_id(self): i32 => self.id;\n"
			"}\n";

	Lexer lex{code};
	Parser parser{lex.tokenize()};
	auto prog = parser.parse_program();
	ASSERT(!parser.has_errors(), "Parsing Widget and impl failed");

	const StructDecl *st = nullptr;
	for (const auto &decl: prog->declarations) {
		if (isa<StructDecl>(decl)) {
			st = as<StructDecl>(decl);
			break;
		}
	}
	ASSERT(st != nullptr, "Widget struct decl not found");
	size_t orig_fields_count = st->fields.size();
	ASSERT(orig_fields_count == 1, "AST StructDecl fields should initially be 1");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic analysis of Widget and impl failed");

	// AST StructDecl must NOT have been mutated
	ASSERT(st->fields.size() == orig_fields_count, "AST StructDecl fields was mutated! Immutability violated");

	// StructSymbol must contain the merged method
	ASSERT(sema.structs.contains("Widget"), "Widget should be in structs symbol table");
	const auto &sym = sema.structs.at("Widget");
	ASSERT(sym.methods.contains("get_id"), "get_id should be in StructSymbol::methods");
	ASSERT(sym.method_decls.size() == 1, "StructSymbol::method_decls should contain 1 method");

	return true;
}

bool test_str_free_internal_linkage() {
	std::string_view code1 =
			"pub fn test_str1(): void {\n"
			"    val s = \"hello\";\n"
			"}\n";
	std::string_view code2 =
			"pub fn test_str2(): void {\n"
			"    val s = \"world\";\n"
			"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code1, ir), "String helper compilation failed");
	ASSERT(ir.find("define internal void @__kobel_str_free(") != std::string::npos,
		   "__kobel_str_free must have internal linkage to avoid duplicate symbols in multi-module linking");

	// Physical object link verification:
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "kobel_linkage_test";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	std::string obj1 = (dir / "mod1.obj").string();
	std::string obj2 = (dir / "mod2.obj").string();
	std::string dll_out = (dir / "out.dll").string();

	auto emit_obj = [&](std::string_view code, const std::string &out_path) -> bool {
		Lexer lex{code};
		Parser parser{lex.tokenize()};
		auto prog = parser.parse_program();
		if (parser.has_errors()) return false;
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		if (diag.has_errors()) return false;
		CodeGen cg{&sema, "test_mod"};
		if (!cg.setup_target_machine()) return false;
		if (!cg.generate(prog)) return false;
		return cg.emit_object_file(out_path);
	};

	ASSERT(emit_obj(code1, obj1), "Emit obj1 failed");
	ASSERT(emit_obj(code2, obj2), "Emit obj2 failed");

	std::string link_cmd = "link /NOLOGO /DLL /NOENTRY /FORCE:UNRESOLVED /OUT:\"" + dll_out + "\" \"" + obj1 + "\" \"" +
						   obj2 + "\" > nul 2>&1";
	int ret = std::system(link_cmd.c_str());
	std::filesystem::remove_all(dir, ec);

	ASSERT(ret == 0, "Physical linking of two modules with string helpers failed due to symbol collision");
	return true;
}

bool test_generic_struct_trait_default_method() {
	std::string_view code =
			"trait Greeter {\n"
			"    fn greet(val self): i32 => 42;\n"
			"}\n"
			"struct Box<T> {\n"
			"    pub item: T\n"
			"}\n"
			"impl<T> Greeter for Box<T> {}\n"
			"fn test_box(): i32 {\n"
			"    val b = Box<i32>(10);\n"
			"    return b.greet();\n"
			"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Generic struct trait default method failed");
	ASSERT(ir.find("Box_i32_greet") != std::string::npos, "Box_i32_greet should be present in IR");
	return true;
}

bool test_generic_struct_multi_trait_impl_override() {
	std::string_view code =
			"trait Named {\n"
			"    fn name(val self): str => \"anonymous\";\n"
			"}\n"
			"trait Value {\n"
			"    fn val_int(val self): i32;\n"
			"    fn double_val(val self): i32 => self.val_int() * 2;\n"
			"}\n"
			"struct Wrapper<T> {\n"
			"    pub item: T\n"
			"}\n"
			"impl<T> Named for Wrapper<T> {}\n"
			"impl<T> Value for Wrapper<T> {\n"
			"    fn val_int(val self): i32 => 10;\n"
			"}\n"
			"fn test_wrap(): i32 {\n"
			"    val w = Wrapper<i32>(100);\n"
			"    val d = w.double_val();\n"
			"    return d;\n"
			"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Generic struct multi-trait impl override failed");
	ASSERT(ir.find("Wrapper_i32_double_val") != std::string::npos, "Wrapper_i32_double_val should be present in IR");
	ASSERT(ir.find("Wrapper_i32_val_int") != std::string::npos, "Wrapper_i32_val_int should be present in IR");
	return true;
}

bool test_struct_with_enum_field() {
	std::string_view code =
			"enum Status {\n"
			"    OK = 0,\n"
			"    ERR = 1\n"
			"}\n"
			"struct Task {\n"
			"    id: i32,\n"
			"    status: Status\n"
			"}\n"
			"fn test_task(): Status {\n"
			"    val t = Task(42, Status.OK);\n"
			"    return t.status;\n"
			"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Compilation of struct with enum field failed");
	ASSERT(ir.find("%Task = type { i32, i32 }") != std::string::npos,
		   "Task struct should have i32 and i32 status in IR");
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

	if (!test_lexer_comment_stack_overflow_stress()) return 1;
	std::cout << "  [PASS] test_lexer_comment_stack_overflow_stress" << std::endl;

	if (!test_multifile_module_isolation()) return 1;
	std::cout << "  [PASS] test_multifile_module_isolation" << std::endl;

	if (!test_array_flyweight_immutability()) return 1;
	std::cout << "  [PASS] test_array_flyweight_immutability" << std::endl;

	if (!test_ast_immutability_struct_and_impl()) return 1;
	std::cout << "  [PASS] test_ast_immutability_struct_and_impl" << std::endl;

	if (!test_str_free_internal_linkage()) return 1;
	std::cout << "  [PASS] test_str_free_internal_linkage" << std::endl;

	if (!test_generic_struct_trait_default_method()) return 1;
	std::cout << "  [PASS] test_generic_struct_trait_default_method" << std::endl;

	if (!test_generic_struct_multi_trait_impl_override()) return 1;
	std::cout << "  [PASS] test_generic_struct_multi_trait_impl_override" << std::endl;

	if (!test_struct_with_enum_field()) return 1;
	std::cout << "  [PASS] test_struct_with_enum_field" << std::endl;

	std::cout << "[ALL PASSED] Latent Fixes Tests passed successfully!" << std::endl;
	return 0;
}
