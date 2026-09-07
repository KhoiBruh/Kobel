#include <iostream>
#include <string_view>

import token;
import lexer;
import ast;
import parser;
import logger;
import semantic;
import semantic.analyzer;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool test_valid_program() {
	std::string_view code = 
		"struct Point(x: i32, y: i32);\n"
		"const ORIGIN: i32 = 0;\n"
		"extern \"libc\" {\n"
		"    fn printf(fmt: *char): i32;\n"
		"}\n"
		"fn compute(p: *Point): i32 {\n"
		"    val base: i32 = 10;\n"
		"    var count: i32 = 0;\n"
		"    while (count < 5) {\n"
		"        count = count + 1;\n"
		"        if (count == 3) {\n"
		"            break;\n"
		"        }\n"
		"    }\n"
		"    val total: i32 = p.x + p.y + base + count;\n"
		"    return total;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser kh??ng ???????c c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "Ch????ng tr??nh h???p l??? kh??ng ???????c c?? l???i ng??? ngh??a");
	return true;
}

bool test_val_immutability_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x: i32 = 10;\n"
		"    x = 20;\n" // L???i: g??n l???i bi???n val
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Ph???i ph??t hi???n l???i khi g??n l???i bi???n val");
	ASSERT(diag.diagnostics[0].format().find("val") != std::string::npos, "Th??ng b??o l???i ph???i nh???c t???i 'val'");
	return true;
}

bool test_missing_type_annotation_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x = 10;\n" // L???i: v0 ch??a cho ph??p suy lu???n ki???u, b???t bu???c ghi r?? : i32
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Ph???i ph??t hi???n l???i thi???u khai b??o ki???u t?????ng minh");
	return true;
}

bool test_strict_type_mismatch_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val a: i32 = 1;\n"
		"    val b: i64 = 2;\n"
		"    val c: i32 = a + b;\n" // L???i: i32 + i64 kh??ng t??? ?????ng th??ng ki???u, b???t bu???c ??p ki???u as
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Ph???i ph??t hi???n l???i kh??ng kh???p ki???u trong ph??p c???ng");
	return true;
}

bool test_explicit_cast_success() {
	std::string_view code = 
		"fn test(a: i32, b: i64): i64 {\n"
		"    val c: i64 = (a as i64) + b;\n" // H???p l??? nh??? ??p ki???u t?????ng minh qua as
		"    return c;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "??p ki???u t?????ng minh qua 'as' ph???i h???p l???");
	return true;
}

bool test_non_boolean_condition_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x: i32 = 1;\n"
		"    if (x) {}\n" // L???i: x c?? ki???u i32, kh??ng ph???i bool
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Ph???i ph??t hi???n l???i ??i???u ki???n if kh??ng ph???i bool");
	return true;
}

bool test_break_outside_loop_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    break;\n" // L???i: break ngo??i v??ng l???p
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Ph???i ph??t hi???n l???i d??ng break ngo??i v??ng l???p");
	return true;
}

bool test_semantic_enum() {
	std::string_view code =
		"enum Status {\n"
		"    OK,\n"
		"    ERROR = 504,\n"
		"    UNKNOWN,\n"
		"}\n"
		"fn check_status(s: Status): i32 {\n"
		"    if (s == Status.OK) {\n"
		"        return 0;\n"
		"    }\n"
		"    val code: i32 = s.value;\n"
		"    val s2: Status = 504 as Status;\n"
		"    val num: i32 = Status.ERROR as i32;\n"
		"    return num;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser kh??ng ???????c c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic kh??ng ???????c c?? l???i v???i enum h???p l???");

	ASSERT(sema.enums.contains("Status"), "Ph???i ch???a enum Status");
	const auto& sym = sema.enums["Status"];
	ASSERT(sym.member_values.at("OK") == 0, "Status.OK ph???i b???ng 0");
	ASSERT(sym.member_values.at("ERROR") == 504, "Status.ERROR ph???i b???ng 504");
	ASSERT(sym.member_values.at("UNKNOWN") == 505, "Status.UNKNOWN ph???i b???ng 505 (t??? t??ng)");

	return true;
}

bool test_semantic_array() {
	std::string_view code =
		"fn test_arr(): i32 {\n"
		"    val a: Array<i32> = [10, 20, 30];\n"
		"    a[0] = 99;\n"
		"    val len: i32 = a.len;\n"
		"    val p: *i32 = a as *i32;\n"
		"    return a[0] + len;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser kh??ng ???????c c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic array h???p l??? kh??ng ???????c c?? l???i");

	return true;
}

bool test_semantic_array_errors() {
	// 1. K??ch th?????c kh??ng kh???p khi khai b??o r?? k??ch th?????c
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val a: Array<i32>(4) = [1, 2, 3];\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Ph???i b??o l???i khi s??? ph???n t??? m???ng kh??c k??ch th?????c khai b??o");
	}

	// 2. Kh??ng th??? g??n l???i bi???n val m???ng (nh??ng ???????c s???a ph???n t???)
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val a: Array<i32> = [1, 2, 3];\n"
			"    a = [4, 5, 6];\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Ph???i b??o l???i khi g??n l???i bi???n m???ng khai b??o b???ng val");
	}

	// 3. Kh??ng th??? g??n gi?? tr??? cho thu???c t??nh .len c???a m???ng
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val a: Array<i32> = [1, 2, 3];\n"
			"    a.len = 10;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Ph???i b??o l???i khi g??n gi?? tr??? cho thu???c t??nh ch??? ?????c .len c???a m???ng");
	}

	return true;
}

bool test_semantic_struct_methods() {
	std::string_view code =
		"struct Point(x: i32, y: i32) {\n"
		"    fn distance_sq(val self): i32 => self.x * self.x + self.y * self.y;\n"
		"    fn translate(var self, dx: i32, dy: i32): void {\n"
		"        self.x = self.x + dx;\n"
		"        self.y = self.y + dy;\n"
		"    }\n"
		"}\n"
		"fn test_methods(): i32 {\n"
		"    var p: Point = Point(3, 4);\n"
		"    val d: i32 = p.distance_sq();\n"
		"    p.translate(1, 2);\n"
		"    return p.distance_sq();\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser kh??ng ???????c c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic struct methods h???p l??? kh??ng ???????c c?? l???i");

	return true;
}

bool test_semantic_struct_method_errors() {
	// 1. Kh???i t???o struct sai s??? l?????ng ?????i s???
	{
		std::string_view code =
			"struct Point(x: i32, y: i32);\n"
			"fn test_err(): void {\n"
			"    val p: Point = Point(1);\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Ph???i b??o l???i khi truy???n thi???u ?????i s??? v??o primary constructor");
	}

	// 2. G???i ph????ng th???c var self tr??n con tr??? ch??? ?????c *T
	{
		std::string_view code =
			"struct Point(x: i32, y: i32) {\n"
			"    fn modify(var self): void { self.x = 0; }\n"
			"}\n"
			"fn test_err(ptr: *Point): void {\n"
			"    ptr.modify();\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Ph???i b??o l???i khi g???i ph????ng th???c var self tr??n con tr??? ch??? ?????c *Point");
	}

	return true;
}

bool test_semantic_logical_operators() {
	// 1. H???p l???: boolean && boolean, boolean || boolean, !boolean
	{
		std::string_view code =
			"fn check(x: i32, flag: bool): bool {\n"
			"    val c1: bool = (x > 0 && x < 100) || !flag;\n"
			"    val c2: bool = flag && (x == 50 || x == 60);\n"
			"    return c1 && c2;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Bi???u th???c logic h???p l??? kh??ng ???????c c?? l???i semantic");
	}

	// 2. Sai ki???u: D??ng s??? nguy??n thay v?? boolean cho &&
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val res: bool = 10 && true;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "To??n t??? '&&' v???i to??n h???ng kh??ng ph???i bool ph???i b??o l???i");
	}

	// 3. Sai ki???u: D??ng s??? nguy??n thay v?? boolean cho ||
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val res: bool = false || 20;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "To??n t??? '||' v???i to??n h???ng kh??ng ph???i bool ph???i b??o l???i");
	}

	// 4. Sai ki???u: D??ng ! tr??n s??? nguy??n
	{
		std::string_view code =
			"fn test_err(): void {\n"
			"    val res: bool = !42;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "To??n t??? '!' v???i to??n h???ng kh??ng ph???i bool ph???i b??o l???i");
	}

	return true;
}

bool test_semantic_modules() {
	std::string_view code =
		"module math.calc;\n"
		"pub fn add(a: i32, b: i32): i32 {\n"
		"    return a + b;\n"
		"}\n"
		"pub const BASE: i32 = 100;\n"
		"\n"
		"module geom;\n"
		"pub struct Point(x: i32, y: i32) {\n"
		"    pub fn sum(val self): i32 {\n"
		"        return self.x + self.y;\n"
		"    }\n"
		"}\n"
		"\n"
		"module app;\n"
		"use math.calc.add;\n"
		"use math.calc.BASE;\n"
		"use math.calc.add as my_add;\n"
		"use geom.Point;\n"
		"\n"
		"fn main(): i32 {\n"
		"    val p: Point = Point(1, 2);\n"
		"    val s: i32 = p.sum();\n"
		"    val r1: i32 = add(s, BASE);\n"
		"    val r2: i32 = my_add(r1, 5);\n"
		"    return r2;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser kh??ng ???????c c?? l???i v???i module syntax");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "Semantic modules h???p l??? kh??ng ???????c c?? l???i");
	return true;
}

bool test_semantic_module_errors() {
	// 1. Private function access error
	{
		std::string_view code =
			"module math.calc;\n"
			"fn secret(): i32 { return 42; }\n"
			"\n"
			"module app;\n"
			"use math.calc.secret;\n"
			"fn main(): i32 { return secret(); }\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Import symbol private ph???i b??o l???i");
		ASSERT(diag.diagnostics[0].format().find("private") != std::string::npos, "L???i ph???i nh???c t???i 'private'");
	}

	// 2. Private method access error
	{
		std::string_view code =
			"module geom;\n"
			"pub struct Point(x: i32, y: i32) {\n"
			"    fn secret_method(val self): i32 { return self.x; }\n"
			"}\n"
			"\n"
			"module app;\n"
			"use geom.Point;\n"
			"fn main(): i32 {\n"
			"    val p: Point = Point(1, 2);\n"
			"    return p.secret_method();\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "G???i ph????ng th???c private t??? module kh??c ph???i b??o l???i");
	}

	// 3. Nonexistent symbol import error
	{
		std::string_view code =
			"module math.calc;\n"
			"pub fn add(a: i32, b: i32): i32 { return a + b; }\n"
			"\n"
			"module app;\n"
			"use math.calc.nonexistent;\n"
			"fn main(): i32 { return 0; }\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Import symbol kh??ng t???n t???i ph???i b??o l???i");
		ASSERT(diag.diagnostics[0].format().find("nonexistent") != std::string::npos, "L???i ph???i nh???c t???i 'nonexistent'");
	}

	return true;
}

bool test_semantic_str_slice() {
	// 1. Valid slice with 1 and 2 arguments
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val s: str = \"Hello, World!\";\n"
			"    val a: str = s.slice(0, 5);\n"
			"    val b: str = s.slice(7);\n"
			"    val len: usz = s.len();\n"
			"    val len_prop: usz = s.len;\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Valid str.slice and str.len should pass semantic analysis");
	}

	// 2. Error: slice with invalid argument type
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val s: str = \"Hello\";\n"
			"    val a: str = s.slice(\"invalid\");\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "str.slice with non-integer argument must report error");
	}

	// 3. Error: slice with 0 arguments
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val s: str = \"Hello\";\n"
			"    val a: str = s.slice();\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "str.slice() with 0 arguments must report error");
	}

	return true;
}

bool test_semantic_type_size() {
	// 1. Valid T.size() on primitives, struct, and enum
	{
		std::string_view code =
			"struct Point(x: i32, y: i32)\n"
			"enum Status { OK, ERR }\n"
			"fn main(): i32 {\n"
			"    val s_i32: usz = i32.size();\n"
			"    val s_i64: usz = i64.size();\n"
			"    val s_u8: usz = u8.size();\n"
			"    val s_bool: usz = bool.size();\n"
			"    val s_char: usz = char.size();\n"
			"    val s_str: usz = str.size();\n"
			"    val s_pt: usz = Point.size();\n"
			"    val s_st: usz = Status.size();\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "T.size() on types must pass semantic analysis");
	}

	// 2. Error: calling .size() on variable of struct that has no size method
	{
		std::string_view code =
			"struct Point(x: i32, y: i32)\n"
			"fn main(): i32 {\n"
			"    val p: Point = Point(1, 2);\n"
			"    val s: usz = p.size();\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Calling .size() on variable without method must fail");
	}

	// 3. Error: T.size() with arguments
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val s: usz = i32.size(10);\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "T.size() with arguments must fail");
	}

	return true;
}

bool test_semantic_when_and_if_expr() {
	// 1. Valid when expression and statement
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val c: i32 = 2;\n"
			"    val x: i32 = when (c) {\n"
			"        1 -> 10;\n"
			"        2, 3 -> 20;\n"
			"        else -> 0;\n"
			"    };\n"
			"    when (x) {\n"
			"        10 -> return 1;\n"
			"        20 -> return 2;\n"
			"        else -> return 0;\n"
			"    }\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Valid when expression and statement must pass semantic check");
	}

	// 2. Valid if expression (braced and unbraced) and unbraced if statement
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val c: bool = true;\n"
			"    val a: i32 = if (c) 1 else 0;\n"
			"    val b: i32 = if (c) { 1 } else { 0 };\n"
			"    if (a > 0) return a; else return b;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Valid if expression and unbraced if statement must pass semantic check");
	}

	// 3. Error: when expression without else arm
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val c: i32 = 1;\n"
			"    val x: i32 = when (c) {\n"
			"        1 -> 10;\n"
			"    };\n"
			"    return x;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "When expression without else arm must report error");
	}

	// 4. Error: if expression branches type mismatch
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val x = if (true) 1 else \"hello\";\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "If expression branch type mismatch must report error");
	}

	// 5. Error: when pattern type incompatible with condition
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val c: i32 = 1;\n"
			"    val x = when (c) {\n"
			"        \"hello\" -> 10;\n"
			"        else -> 0;\n"
			"    };\n"
			"    return x;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "When pattern type mismatch with condition must report error");
	}

	return true;
}

int main() {
	std::cout << "[RUNNING] Semantic tests..." << std::endl;

	if (!test_valid_program()) return 1;
	std::cout << "  [PASS] test_valid_program" << std::endl;

	if (!test_val_immutability_error()) return 1;
	std::cout << "  [PASS] test_val_immutability_error" << std::endl;

	if (!test_missing_type_annotation_error()) return 1;
	std::cout << "  [PASS] test_missing_type_annotation_error" << std::endl;

	if (!test_strict_type_mismatch_error()) return 1;
	std::cout << "  [PASS] test_strict_type_mismatch_error" << std::endl;

	if (!test_explicit_cast_success()) return 1;
	std::cout << "  [PASS] test_explicit_cast_success" << std::endl;

	if (!test_non_boolean_condition_error()) return 1;
	std::cout << "  [PASS] test_non_boolean_condition_error" << std::endl;

	if (!test_break_outside_loop_error()) return 1;
	std::cout << "  [PASS] test_break_outside_loop_error" << std::endl;

	if (!test_semantic_enum()) return 1;
	std::cout << "  [PASS] test_semantic_enum" << std::endl;

	if (!test_semantic_array()) return 1;
	std::cout << "  [PASS] test_semantic_array" << std::endl;

	if (!test_semantic_array_errors()) return 1;
	std::cout << "  [PASS] test_semantic_array_errors" << std::endl;

	if (!test_semantic_struct_methods()) return 1;
	std::cout << "  [PASS] test_semantic_struct_methods" << std::endl;

	if (!test_semantic_struct_method_errors()) return 1;
	std::cout << "  [PASS] test_semantic_struct_method_errors" << std::endl;

	if (!test_semantic_logical_operators()) return 1;
	std::cout << "  [PASS] test_semantic_logical_operators" << std::endl;

	if (!test_semantic_modules()) return 1;
	std::cout << "  [PASS] test_semantic_modules" << std::endl;

	if (!test_semantic_module_errors()) return 1;
	std::cout << "  [PASS] test_semantic_module_errors" << std::endl;

	if (!test_semantic_str_slice()) return 1;
	std::cout << "  [PASS] test_semantic_str_slice" << std::endl;

	if (!test_semantic_type_size()) return 1;
	std::cout << "  [PASS] test_semantic_type_size" << std::endl;

	if (!test_semantic_when_and_if_expr()) return 1;
	std::cout << "  [PASS] test_semantic_when_and_if_expr" << std::endl;

	std::cout << "[ALL PASSED] Semantic tests passed successfully!" << std::endl;
	return 0;
}





