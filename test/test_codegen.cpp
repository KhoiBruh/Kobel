#include <filesystem>
#include <fstream>
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

// ============================================================================
// Giai đoạn 2: Native Target, Object File, Assembly & Executable
// ============================================================================

bool test_codegen_target_machine() {
	DiagnosticEngine diag;
	Analyzer sema{diag};
	CodeGen cg{&sema, "test_tm"};

	ASSERT(cg.target_machine != nullptr, "TargetMachine chưa được khởi tạo");
	ASSERT(!cg.module->getDataLayout().getStringRepresentation().empty(), "DataLayout của Module bị rỗng");
	ASSERT(cg.module->getTargetTriple().str().find("x86_64") != std::string::npos, "TargetTriple không phải x86_64");
	return true;
}

bool test_codegen_emit_object_file() {
	std::string_view code =
		"fn multiply(a: i32, b: i32): i32 {\n"
		"    return a * b;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic có lỗi");

	CodeGen cg{&sema, "test_obj"};
	ASSERT(cg.generate(prog.get()), "CodeGen generate thất bại");

	const std::string obj_file = "test_multiply.obj";
	std::filesystem::remove(obj_file);

	ASSERT(cg.emit_object_file(obj_file), "emit_object_file thất bại");
	ASSERT(std::filesystem::exists(obj_file), "File object không tồn tại");
	ASSERT(std::filesystem::file_size(obj_file) > 0, "File object có kích thước rỗng");

	std::filesystem::remove(obj_file);
	return true;
}

bool test_codegen_emit_assembly() {
	std::string_view code =
		"fn sub(a: i32, b: i32): i32 {\n"
		"    return a - b;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic có lỗi");

	CodeGen cg{&sema, "test_asm"};
	ASSERT(cg.generate(prog.get()), "CodeGen generate thất bại");

	const std::string asm_file = "test_sub.s";
	std::filesystem::remove(asm_file);

	ASSERT(cg.emit_assembly_file(asm_file), "emit_assembly_file thất bại");
	ASSERT(std::filesystem::exists(asm_file), "File assembly không tồn tại");

	std::ifstream f(asm_file);
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	ASSERT(content.find("sub") != std::string::npos, "Assembly không chứa nhãn hàm 'sub'");

	f.close();
	std::filesystem::remove(asm_file);
	return true;
}

bool test_codegen_e2e_executable() {
	std::string_view code =
		"extern \"libc\" {\n"
		"    fn puts(str: *char): i32;\n"
		"}\n"
		"fn main(): i32 {\n"
		"    puts(\"Hello from native Kobel executable!\");\n"
		"    return 42;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser có lỗi");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog.get());
	ASSERT(!diag.has_errors(), "Semantic có lỗi");

	CodeGen cg{&sema, "e2e_module"};
	ASSERT(cg.generate(prog.get()), "CodeGen generate thất bại");

	const std::string obj_file = "e2e_kobel.obj";
	const std::string exe_file = "e2e_kobel.exe";

	std::filesystem::remove(obj_file);
	std::filesystem::remove(exe_file);

	ASSERT(cg.emit_object_file(obj_file), "Sinh file object e2e thất bại");
	ASSERT(CodeGen::link_executable(obj_file, exe_file), "Link executable e2e thất bại");
	ASSERT(std::filesystem::exists(exe_file), "File thực thi e2e_kobel.exe không tồn tại");

	int exit_code = std::system(".\\e2e_kobel.exe");
	ASSERT(exit_code == 42, "Exit code của e2e_kobel.exe mong đợi 42, nhận được " + std::to_string(exit_code));

	std::filesystem::remove(obj_file);
	std::filesystem::remove(exe_file);
	return true;
}

bool test_codegen_enum() {
	std::string_view code =
		"enum Status {\n"
		"    OK,\n"
		"    ERROR = 504,\n"
		"    NEXT,\n"
		"}\n"
		"fn test_enum(): i32 {\n"
		"    val s: Status = Status.ERROR;\n"
		"    val val1: i32 = s.value;\n"
		"    val val2: i32 = Status.NEXT as i32;\n"
		"    val s3: Status = 0 as Status;\n"
		"    if (s3 == Status.OK) {\n"
		"        return val1 + val2;\n"
		"    }\n"
		"    return 0;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã enum thất bại");
	ASSERT(ir.find("504") != std::string::npos, "Thiếu hằng số 504 trong IR");
	ASSERT(ir.find("505") != std::string::npos, "Thiếu hằng số 505 trong IR");
	return true;
}

bool test_codegen_array() {
	std::string_view code =
		"fn process_array(): i32 {\n"
		"    val a: Array<i32> = [10, 20, 30];\n"
		"    a[0] = 99;\n"
		"    val len: i32 = a.len;\n"
		"    val p: *i32 = a as *i32;\n"
		"    return a[0] + len;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Phát sinh mã Array thất bại");
	ASSERT(ir.find("[3 x i32]") != std::string::npos, "Thiếu kiểu [3 x i32] trong LLVM IR");
	ASSERT(ir.find("arrayidx") != std::string::npos, "Thiếu GEP arrayidx trong LLVM IR");
	ASSERT(ir.find("arraydecay") != std::string::npos, "Thiếu GEP arraydecay trong LLVM IR");
	return true;
}

int main() {
	int passed = 0;
	int total = 12;

	std::cout << "Running CodeGen Tests (Stage 1 & Stage 2)...\n";

	// Stage 1
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
	if (test_codegen_enum()) {
		std::cout << "[PASS] test_codegen_enum\n";
		passed++;
	}
	if (test_codegen_array()) {
		std::cout << "[PASS] test_codegen_array\n";
		passed++;
	}

	// Stage 2
	if (test_codegen_target_machine()) {
		std::cout << "[PASS] test_codegen_target_machine\n";
		passed++;
	}
	if (test_codegen_emit_object_file()) {
		std::cout << "[PASS] test_codegen_emit_object_file\n";
		passed++;
	}
	if (test_codegen_emit_assembly()) {
		std::cout << "[PASS] test_codegen_emit_assembly\n";
		passed++;
	}
	if (test_codegen_e2e_executable()) {
		std::cout << "[PASS] test_codegen_e2e_executable\n";
		passed++;
	}

	std::cout << "\nCodeGen Results: " << passed << "/" << total << " passed.\n";
	return (passed == total) ? 0 : 1;
}
