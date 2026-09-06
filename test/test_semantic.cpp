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
	ASSERT(!p.has_errors(), "Parser không được có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "Chương trình hợp lệ không được có lỗi ngữ nghĩa");
	return true;
}

bool test_val_immutability_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x: i32 = 10;\n"
		"    x = 20;\n" // Lỗi: gán lại biến val
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	ASSERT(diag.has_errors(), "Phải phát hiện lỗi khi gán lại biến val");
	ASSERT(diag.diagnostics[0].format().find("val") != std::string::npos, "Thông báo lỗi phải nhắc tới 'val'");
	return true;
}

bool test_missing_type_annotation_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x = 10;\n" // Lỗi: v0 chưa cho phép suy luận kiểu, bắt buộc ghi rõ : i32
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	ASSERT(diag.has_errors(), "Phải phát hiện lỗi thiếu khai báo kiểu tường minh");
	return true;
}

bool test_strict_type_mismatch_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val a: i32 = 1;\n"
		"    val b: i64 = 2;\n"
		"    val c: i32 = a + b;\n" // Lỗi: i32 + i64 không tự động thăng kiểu, bắt buộc ép kiểu as
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	ASSERT(diag.has_errors(), "Phải phát hiện lỗi không khớp kiểu trong phép cộng");
	return true;
}

bool test_explicit_cast_success() {
	std::string_view code = 
		"fn test(a: i32, b: i64): i64 {\n"
		"    val c: i64 = (a as i64) + b;\n" // Hợp lệ nhờ ép kiểu tường minh qua as
		"    return c;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
	}
	ASSERT(!diag.has_errors(), "Ép kiểu tường minh qua 'as' phải hợp lệ");
	return true;
}

bool test_non_boolean_condition_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    val x: i32 = 1;\n"
		"    if (x) {}\n" // Lỗi: x có kiểu i32, không phải bool
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	ASSERT(diag.has_errors(), "Phải phát hiện lỗi điều kiện if không phải bool");
	return true;
}

bool test_break_outside_loop_error() {
	std::string_view code = 
		"fn test(): void {\n"
		"    break;\n" // Lỗi: break ngoài vòng lặp
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());

	ASSERT(diag.has_errors(), "Phải phát hiện lỗi dùng break ngoài vòng lặp");
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
	ASSERT(!p.has_errors(), "Parser không được có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic không được có lỗi với enum hợp lệ");

	ASSERT(sema.enums.contains("Status"), "Phải chứa enum Status");
	const auto& sym = sema.enums["Status"];
	ASSERT(sym.member_values.at("OK") == 0, "Status.OK phải bằng 0");
	ASSERT(sym.member_values.at("ERROR") == 504, "Status.ERROR phải bằng 504");
	ASSERT(sym.member_values.at("UNKNOWN") == 505, "Status.UNKNOWN phải bằng 505 (tự tăng)");

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
	ASSERT(!p.has_errors(), "Parser không được có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic array hợp lệ không được có lỗi");

	return true;
}

bool test_semantic_array_errors() {
	// 1. Kích thước không khớp khi khai báo rõ kích thước
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Phải báo lỗi khi số phần tử mảng khác kích thước khai báo");
	}

	// 2. Không thể gán lại biến val mảng (nhưng được sửa phần tử)
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Phải báo lỗi khi gán lại biến mảng khai báo bằng val");
	}

	// 3. Không thể gán giá trị cho thuộc tính .len của mảng
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Phải báo lỗi khi gán giá trị cho thuộc tính chỉ đọc .len của mảng");
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
	ASSERT(!p.has_errors(), "Parser không được có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic struct methods hợp lệ không được có lỗi");

	return true;
}

bool test_semantic_struct_method_errors() {
	// 1. Khởi tạo struct sai số lượng đối số
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Phải báo lỗi khi truyền thiếu đối số vào primary constructor");
	}

	// 2. Gọi phương thức var self trên con trỏ chỉ đọc *T
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Phải báo lỗi khi gọi phương thức var self trên con trỏ chỉ đọc *Point");
	}

	return true;
}

bool test_semantic_logical_operators() {
	// 1. Hợp lệ: boolean && boolean, boolean || boolean, !boolean
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
		sema.analyze(prog.get());
		ASSERT(!diag.has_errors(), "Biểu thức logic hợp lệ không được có lỗi semantic");
	}

	// 2. Sai kiểu: Dùng số nguyên thay vì boolean cho &&
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Toán tử '&&' với toán hạng không phải bool phải báo lỗi");
	}

	// 3. Sai kiểu: Dùng số nguyên thay vì boolean cho ||
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Toán tử '||' với toán hạng không phải bool phải báo lỗi");
	}

	// 4. Sai kiểu: Dùng ! trên số nguyên
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
		sema.analyze(prog.get());
		ASSERT(diag.has_errors(), "Toán tử '!' với toán hạng không phải bool phải báo lỗi");
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

	std::cout << "[ALL PASSED] Semantic tests passed successfully!" << std::endl;
	return 0;
}

