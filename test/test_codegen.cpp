#include <iostream>
#include <string_view>
#include <string>

import token;
import lexer;
import ast;
import parser;
import logger;
import semantic;
import semantic.analyzer;
import codegen;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool compile_to_ir(std::string_view code, std::string& out_ir) {
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	if (p.has_errors()) {
		std::cerr << "Parser error in test code\n";
		return false;
	}

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	if (diag.has_errors()) {
		diag.print_all(std::cerr);
		return false;
	}

	CodeGen cg{&sema, "test_module"};
	if (!cg.generate(prog.get())) {
		std::cerr << "CodeGen LLVM verification failed!\n";
		std::cerr << "Dump IR:\n" << cg.dump_ir() << std::endl;
		return false;
	}

	out_ir = cg.dump_ir();
	return true;
}

bool test_codegen_arithmetic() {
	std::string_view code =
		"fn add(a: i32, b: i32): i32 {\n"
		"    return a + b;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã add thất bại");
	ASSERT(ir.find("define i32 @add(") != std::string::npos, "Thiếu định nghĩa hàm @add");
	ASSERT(ir.find("add i32") != std::string::npos, "Thiếu lệnh add i32");
	ASSERT(ir.find("ret i32") != std::string::npos, "Thiếu lệnh ret i32");
	return true;
}

bool test_codegen_variables() {
	std::string_view code =
		"fn compute(): i32 {\n"
		"    val x: i32 = 10;\n"
		"    var y: i32 = 20;\n"
		"    y = y + x;\n"
		"    return y;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã compute thất bại");
	ASSERT(ir.find("alloca i32") != std::string::npos, "Thiếu alloca i32");
	ASSERT(ir.find("store i32 10") != std::string::npos, "Thiếu store 10 cho val x");
	ASSERT(ir.find("store i32 20") != std::string::npos, "Thiếu store 20 cho var y");
	ASSERT(ir.find("load i32") != std::string::npos, "Thiếu load i32");
	ASSERT(ir.find("ret i32") != std::string::npos, "Thiếu ret i32");
	return true;
}

bool test_codegen_control_flow() {
	std::string_view code =
		"fn loop_sum(): i32 {\n"
		"    var i: i32 = 0;\n"
		"    var sum: i32 = 0;\n"
		"    while (i < 10) {\n"
		"        if (i == 5) {\n"
		"            break;\n"
		"        }\n"
		"        sum = sum + i;\n"
		"        i = i + 1;\n"
		"    }\n"
		"    return sum;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã loop_sum thất bại");
	ASSERT(ir.find("icmp slt i32") != std::string::npos, "Thiếu so sánh i < 10");
	ASSERT(ir.find("icmp eq i32") != std::string::npos, "Thiếu so sánh i == 5");
	ASSERT(ir.find("br i1") != std::string::npos, "Thiếu rẽ nhánh điều kiện");
	return true;
}

bool test_codegen_struct_and_pointer() {
	std::string_view code =
		"struct Point(x: i32, y: i32);\n"
		"fn get_x(p: *Point): i32 {\n"
		"    return p.x;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã struct Point thất bại");
	ASSERT(ir.find("%Point = type { i32, i32 }") != std::string::npos, "Thiếu định nghĩa struct %Point");
	ASSERT(ir.find("getelementptr inbounds") != std::string::npos && ir.find("%Point, ptr") != std::string::npos, "Thiếu GEP cho p.x");
	return true;
}

bool test_codegen_extern_and_call() {
	std::string_view code =
		"extern \"libc\" {\n"
		"    fn puts(str: *char): i32;\n"
		"}\n"
		"fn main(): i32 {\n"
		"    puts(\"Hello, Kobel!\");\n"
		"    return 0;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã extern puts & main thất bại");
	ASSERT(ir.find("declare i32 @puts(ptr") != std::string::npos, "Thiếu khai báo declare puts");
	ASSERT(ir.find("call i32 @puts(") != std::string::npos, "Thiếu lệnh gọi call @puts");
	ASSERT(ir.find("Hello, Kobel!") != std::string::npos, "Thiếu hằng chuỗi Hello, Kobel!");
	return true;
}

bool test_codegen_cast() {
	std::string_view code =
		"fn cast_up(x: i8): i32 {\n"
		"    return x as i32;\n"
		"}\n"
		"fn cast_down(x: i32): i8 {\n"
		"    return x as i8;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã type cast thất bại");
	ASSERT(ir.find("sext i8") != std::string::npos, "Thiếu lệnh sext cho i8 -> i32");
	ASSERT(ir.find("trunc i32") != std::string::npos, "Thiếu lệnh trunc cho i32 -> i8");
	return true;
}

int main() {
	int passed = 0;
	int total = 6;

	std::cout << "Running CodeGen Tests...\n";

	if (test_codegen_arithmetic()) {
		std::cout << "[PASS] test_codegen_arithmetic\n";
		passed++;
	}
	if (test_codegen_variables()) {
		std::cout << "[PASS] test_codegen_variables\n";
		passed++;
	}
	if (test_codegen_control_flow()) {
		std::cout << "[PASS] test_codegen_control_flow\n";
		passed++;
	}
	if (test_codegen_struct_and_pointer()) {
		std::cout << "[PASS] test_codegen_struct_and_pointer\n";
		passed++;
	}
	if (test_codegen_extern_and_call()) {
		std::cout << "[PASS] test_codegen_extern_and_call\n";
		passed++;
	}
	if (test_codegen_cast()) {
		std::cout << "[PASS] test_codegen_cast\n";
		passed++;
	}

	std::cout << "\nCodeGen Results: " << passed << "/" << total << " passed.\n";
	return (passed == total) ? 0 : 1;
}
