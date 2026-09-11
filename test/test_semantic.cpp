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
		"    var x;\n" // Loi: khong co kieu va khong co initializer
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	ASSERT(diag.has_errors(), "Phai phat hien loi thieu kieu va initializer");
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

bool test_semantic_generic_structs() {
	// 1. Generic struct definition, explicit & inferred instantiation, field access, nested
	{
		std::string_view code =
			"struct Box<T>(value: T)\n"
			"struct Pair<T, U>(first: T, second: U)\n"
			"fn main(): i32 {\n"
			"    val b: Box<i32> = Box<i32>(42);\n"
			"    val p: Pair<i32, str> = Pair<i32, str>(1, \"hello\");\n"
			"    val b_inferred: Box<i32> = Box(100);\n"
			"    val p_inferred: Pair<i32, str> = Pair(2, \"world\");\n"
			"    val v: i32 = b.value;\n"
			"    val s: str = p.second;\n"
			"    val nested: Box<Box<i32>> = Box<Box<i32>>(b);\n"
			"    val nested_v: i32 = nested.value.value;\n"
			"    return v + nested_v;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Generic structs valid program must pass semantic analysis");
	}

	// 2. Generic struct with methods
	{
		std::string_view code =
			"struct Box<T>(value: T) {\n"
			"    fn get(val self): T => self.value;\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val b: Box<i32> = Box<i32>(42);\n"
			"    val res: i32 = b.get();\n"
			"    return res;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Generic struct methods must pass semantic analysis");
	}

	return true;
}

bool test_semantic_generic_struct_errors() {
	// 1. Field type mismatch in instantiation
	{
		std::string_view code =
			"struct Box<T>(value: T)\n"
			"fn main(): i32 {\n"
			"    val b: Box<i32> = Box<i32>(\"not an int\");\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Field type mismatch in generic struct instantiation must report error");
	}

	// 2. Wrong number of type arguments
	{
		std::string_view code =
			"struct Box<T>(value: T)\n"
			"fn main(): i32 {\n"
			"    val b: Box<i32, str> = 0;\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Wrong number of type arguments must report error");
	}

	// 3. Generic struct used without type arguments
	{
		std::string_view code =
			"struct Box<T>(value: T)\n"
			"fn main(): i32 {\n"
			"    val b: Box = 0;\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Generic struct used as type without arguments must report error");
	}

	// 4. Unknown generic struct
	{
		std::string_view code =
			"fn main(): i32 {\n"
			"    val b: Unknown<i32> = 0;\n"
			"    return 0;\n"
			"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Unknown generic struct must report error");
	}

	return true;
}

bool test_variable_and_fn_type_inference() {
	std::string_view code =
		"fn add(a: i32, b: i32) => a + b;\n"
		"fn check(x: i32) => if (x > 0) true else false;\n"
		"fn describe(x: i32) => when (x) { 0 -> \"zero\"; else -> \"other\"; };\n"
		"fn test_inference(): i32 {\n"
		"    val num = 42;\n"
		"    val flag = true;\n"
		"    val ch = 'k';\n"
		"    val text = \"hello\";\n"
		"    val if_res = if (flag) 100 else 200;\n"
		"    val when_res = when (num) { 42 -> \"matched\"; else -> \"unmatched\"; };\n"
		"    val sum = add(num, if_res);\n"
		"    val is_pos = check(sum);\n"
		"    val desc = describe(0);\n"
		"    return sum;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser should have no errors");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "Type inference should succeed with no errors");

	// Verify inferred function return types
	ASSERT(sema.functions.contains("add"), "add must be registered");
	ASSERT(sema.functions.at("add").return_type->is_integer(), "add return type must be inferred as i32");
	ASSERT(sema.functions.contains("check"), "check must be registered");
	ASSERT(sema.functions.at("check").return_type->is_bool(), "check return type must be inferred as bool");
	ASSERT(sema.functions.contains("describe"), "describe must be registered");
	ASSERT(sema.functions.at("describe").return_type->is_str(), "describe return type must be inferred as str");

	return true;
}

bool test_semantic_generic_functions() {
	// 1. Generic function with explicit and inferred calls
	{
		std::string_view code =
			"struct Pair<T, U>(first: T, second: U)\n"
			"fn id<T>(x: T): T => x;\n"
			"fn max<T>(a: T, b: T): T {\n"
			"    if (a > b) return a; else return b;\n"
			"}\n"
			"fn make_pair<T, U>(a: T, b: U): Pair<T, U> => Pair(a, b);\n"
			"fn main(): i32 {\n"
			"    val a: i32 = id<i32>(42);\n"
			"    val b: i32 = id(42);\n"
			"    val c: str = id(\"hello\");\n"
			"    val m: i64 = max(10L, 20L);\n"
			"    val p = make_pair(100, \"world\");\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		ASSERT(!p.has_errors(), "Parser should have no errors");

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);

		if (diag.has_errors()) {
			diag.print_all(std::cerr);
		}
		ASSERT(!diag.has_errors(), "Generic function analysis should succeed with no errors");

		// Check instantiated functions in symbol table
		ASSERT(sema.functions.contains("id<i32>"), "id<i32> must be instantiated");
		ASSERT(sema.functions.contains("id<str>"), "id<str> must be instantiated");
		ASSERT(sema.functions.contains("max<i64>"), "max<i64> must be instantiated");
		ASSERT(sema.functions.contains("make_pair<i32, str>"), "make_pair<i32, str> must be instantiated");
	}

	// 2. Error: type count mismatch
	{
		std::string_view code =
			"fn id<T>(x: T): T => x;\n"
			"fn main(): i32 {\n"
			"    val a = id<i32, str>(42);\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Generic function with wrong number of type args must error");
	}

	// 3. Error: cannot infer type parameter
	{
		std::string_view code =
			"fn dummy<T>(): i32 => 0;\n"
			"fn main(): i32 {\n"
			"    val a = dummy();\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Uninferrable generic function call without type args must error");
	}

	return true;
}

bool test_semantic_module_prefixes() {
	// 1. Ambiguous bare import collision resolved by module prefix
	{
		std::string_view code =
			"module math.vec;\n"
			"pub struct Vector(x: i32, y: i32)\n"
			"module physics.space;\n"
			"pub struct Vector(mag: i32)\n"
			"module main;\n"
			"use math.vec.Vector;\n"
			"use physics.space.Vector;\n"
			"fn main(): i32 {\n"
			"    val v1: vec.Vector = vec.Vector(10, 20);\n"
			"    val v2: space.Vector = space.Vector(100);\n"
			"    return v1.x + v2.mag;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		ASSERT(!p.has_errors(), "Parser should have no errors");

		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);

		if (diag.has_errors()) {
			diag.print_all(std::cerr);
		}
		ASSERT(!diag.has_errors(), "Disambiguated module prefix access must succeed");
	}

	// 2. Bare symbol collision reports ambiguous error
	{
		std::string_view code =
			"module math.vec;\n"
			"pub struct Vector(x: i32, y: i32)\n"
			"module physics.space;\n"
			"pub struct Vector(mag: i32)\n"
			"module main;\n"
			"use math.vec.Vector;\n"
			"use physics.space.Vector;\n"
			"fn main(): i32 {\n"
			"    val v = Vector(10, 20);\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Accessing ambiguous bare imported symbol must report error");
	}

	return true;
}

bool test_semantic_traits() {
	// 1. Basic trait with required and default method, called on struct
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"    fn greet(val self): str => \"hello\";\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"    override fn name(val self): str => self.first_name;\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val p = Person(\"Kobel\");\n"
			"    val n: str = p.name();\n"
			"    val g: str = p.greet();\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Valid trait implementation and call must pass semantic analysis");
	}

	// 2. Trait inheritance
	{
		std::string_view code =
			"trait Base {\n"
			"    fn base_val(val self): i32 => 10;\n"
			"}\n"
			"trait Derived : Base {\n"
			"    fn derived_val(val self): i32;\n"
			"}\n"
			"struct Foo() : Derived {\n"
			"    override fn derived_val(val self): i32 => 20;\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val f = Foo();\n"
			"    val b = f.base_val();\n"
			"    val d = f.derived_val();\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Trait inheritance must pass semantic analysis");
	}

	// 3. Generic function with trait bound
	{
		std::string_view code =
			"trait Printable {\n"
			"    fn print_me(val self): str;\n"
			"}\n"
			"struct Item(msg: str) : Printable {\n"
			"    override fn print_me(val self): str => self.msg;\n"
			"}\n"
			"fn show<T: Printable>(val x: T): str {\n"
			"    return x.print_me();\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val it = Item(\"hello\");\n"
			"    val s: str = show<Item>(it);\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Generic function with satisfied trait bound must pass");
	}

	// 4. Generic struct with trait bound
	{
		std::string_view code =
			"trait Hashable {\n"
			"    fn hash(val self): i64;\n"
			"}\n"
			"struct Key(k_id: i64) : Hashable {\n"
			"    override fn hash(val self): i64 => self.k_id;\n"
			"}\n"
			"struct Container<T: Hashable>(item: T) {\n"
			"    fn get_hash(val self): i64 => self.item.hash();\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val k = Key(12345L);\n"
			"    val c = Container<Key>(k);\n"
			"    val h = c.get_hash();\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(!diag.has_errors(), "Generic struct with satisfied trait bound must pass");
	}

	return true;
}

bool test_semantic_trait_errors() {
	// 1. Error: struct missing required method from trait
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Struct missing required trait method must report error");
	}

	// 2. Error: method marked override but struct implements no traits
	{
		std::string_view code =
			"struct Person(first_name: str) {\n"
			"    override fn foo(val self): str => self.first_name;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Method marked override in struct without traits must report error");
	}

	// 3. Error: method marked override but does not match any trait method
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"    override fn name(val self): str => self.first_name;\n"
			"    override fn extra(val self): i32 => 42;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Method marked override not in trait must report error");
	}

	// 4. Error: method overrides trait method but forgets 'override'
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"    fn name(val self): str => self.first_name;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Trait method implementation without 'override' must report error");
	}

	// 5. Error: method receiver mode mismatch (var self vs val self)
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"    override fn name(var self): str => self.first_name;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Method receiver mode mismatch must report error");
	}

	// 6. Error: method return type mismatch (i32 vs str)
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct Person(first_name: str) : Greeter {\n"
			"    override fn name(val self): i32 => 42;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Method return type mismatch must report error");
	}

	// 7. Error: generic function trait bound not satisfied
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct NotGreeter(x: i32)\n"
			"fn test_bound<T: Greeter>(val x: T): str => x.name();\n"
			"fn main(): i32 {\n"
			"    val ng = NotGreeter(42);\n"
			"    val s = test_bound<NotGreeter>(ng);\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Generic function with unsatisfied trait bound must report error");
	}

	// 8. Error: generic struct trait bound not satisfied
	{
		std::string_view code =
			"trait Greeter {\n"
			"    fn name(val self): str;\n"
			"}\n"
			"struct NotGreeter(x: i32)\n"
			"struct Box<T: Greeter>(val item: T)\n"
			"fn main(): i32 {\n"
			"    val ng = NotGreeter(42);\n"
			"    val b = Box<NotGreeter>(ng);\n"
			"    return 0;\n"
			"}\n";

		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto *prog = p.parse_program();
		DiagnosticEngine diag;
		Analyzer sema{diag};
		sema.analyze(prog);
		ASSERT(diag.has_errors(), "Generic struct with unsatisfied trait bound must report error");
	}

	return true;
}

int main() {
	std::cout << "[RUNNING] Semantic tests..." << std::endl;

	if (!test_semantic_traits()) return 1;
	std::cout << "  [PASS] test_semantic_traits" << std::endl;

	if (!test_semantic_trait_errors()) return 1;
	std::cout << "  [PASS] test_semantic_trait_errors" << std::endl;

	if (!test_valid_program()) return 1;
	std::cout << "  [PASS] test_valid_program" << std::endl;

	if (!test_val_immutability_error()) return 1;
	std::cout << "  [PASS] test_val_immutability_error" << std::endl;

	if (!test_missing_type_annotation_error()) return 1;
	std::cout << "  [PASS] test_missing_type_annotation_error" << std::endl;

	if (!test_variable_and_fn_type_inference()) return 1;
	std::cout << "  [PASS] test_variable_and_fn_type_inference" << std::endl;

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

	if (!test_semantic_generic_structs()) return 1;
	std::cout << "  [PASS] test_semantic_generic_structs" << std::endl;

	if (!test_semantic_generic_struct_errors()) return 1;
	std::cout << "  [PASS] test_semantic_generic_struct_errors" << std::endl;

	if (!test_semantic_generic_functions()) return 1;
	std::cout << "  [PASS] test_semantic_generic_functions" << std::endl;

	if (!test_semantic_module_prefixes()) return 1;
	std::cout << "  [PASS] test_semantic_module_prefixes" << std::endl;

	std::cout << "[ALL PASSED] Semantic tests passed successfully!" << std::endl;
	return 0;
}





