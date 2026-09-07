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
	sema.analyze(prog);
	if (diag.has_errors()) {
		diag.print_all(std::cerr);
		return false;
	}

	CodeGen cg{&sema, "test_module"};
	if (!cg.generate(prog)) {
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? add th???t b???i");
	ASSERT(ir.find("define i32 @add(") != std::string::npos, "Thi???u ?????nh ngh??a h??m @add");
	ASSERT(ir.find("add i32") != std::string::npos, "Thi???u l???nh add i32");
	ASSERT(ir.find("ret i32") != std::string::npos, "Thi???u l???nh ret i32");
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? compute th???t b???i");
	ASSERT(ir.find("alloca i32") != std::string::npos, "Thi???u alloca i32");
	ASSERT(ir.find("store i32 10") != std::string::npos, "Thi???u store 10 cho val x");
	ASSERT(ir.find("store i32 20") != std::string::npos, "Thi???u store 20 cho var y");
	ASSERT(ir.find("load i32") != std::string::npos, "Thi???u load i32");
	ASSERT(ir.find("ret i32") != std::string::npos, "Thi???u ret i32");
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? loop_sum th???t b???i");
	ASSERT(ir.find("icmp slt i32") != std::string::npos, "Thi???u so s??nh i < 10");
	ASSERT(ir.find("icmp eq i32") != std::string::npos, "Thi???u so s??nh i == 5");
	ASSERT(ir.find("br i1") != std::string::npos, "Thi???u r??? nh??nh ??i???u ki???n");
	return true;
}

bool test_codegen_struct_and_pointer() {
	std::string_view code =
		"struct Point(x: i32, y: i32);\n"
		"fn get_x(p: *Point): i32 {\n"
		"    return p.x;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? struct Point th???t b???i");
	ASSERT(ir.find("%Point = type { i32, i32 }") != std::string::npos, "Thi???u ?????nh ngh??a struct %Point");
	ASSERT(ir.find("getelementptr inbounds") != std::string::npos && ir.find("%Point, ptr") != std::string::npos, "Thi???u GEP cho p.x");
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? extern puts & main th???t b???i");
	ASSERT(ir.find("declare i32 @puts(ptr") != std::string::npos, "Thi???u khai b??o declare puts");
	ASSERT(ir.find("call i32 @puts(") != std::string::npos, "Thi???u l???nh g???i call @puts");
	ASSERT(ir.find("Hello, Kobel!") != std::string::npos, "Thi???u h???ng chu???i Hello, Kobel!");
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? type cast th???t b???i");
	ASSERT(ir.find("sext i8") != std::string::npos, "Thi???u l???nh sext cho i8 -> i32");
	ASSERT(ir.find("trunc i32") != std::string::npos, "Thi???u l???nh trunc cho i32 -> i8");
	return true;
}

// ============================================================================
// Giai ??o???n 2: Native Target, Object File, Assembly & Executable
// ============================================================================

bool test_codegen_target_machine() {
	DiagnosticEngine diag;
	Analyzer sema{diag};
	CodeGen cg{&sema, "test_tm"};

	ASSERT(cg.target_machine != nullptr, "TargetMachine ch??a ???????c kh???i t???o");
	ASSERT(!cg.module->getDataLayout().getStringRepresentation().empty(), "DataLayout c???a Module b??? r???ng");
	ASSERT(cg.module->getTargetTriple().str().find("x86_64") != std::string::npos, "TargetTriple kh??ng ph???i x86_64");
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
	ASSERT(!p.has_errors(), "Parser c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic c?? l???i");

	CodeGen cg{&sema, "test_obj"};
	ASSERT(cg.generate(prog), "CodeGen generate th???t b???i");

	const std::string obj_file = "test_multiply.obj";
	std::filesystem::remove(obj_file);

	ASSERT(cg.emit_object_file(obj_file), "emit_object_file th???t b???i");
	ASSERT(std::filesystem::exists(obj_file), "File object kh??ng t???n t???i");
	ASSERT(std::filesystem::file_size(obj_file) > 0, "File object c?? k??ch th?????c r???ng");

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
	ASSERT(!p.has_errors(), "Parser c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic c?? l???i");

	CodeGen cg{&sema, "test_asm"};
	ASSERT(cg.generate(prog), "CodeGen generate th???t b???i");

	const std::string asm_file = "test_sub.s";
	std::filesystem::remove(asm_file);

	ASSERT(cg.emit_assembly_file(asm_file), "emit_assembly_file th???t b???i");
	ASSERT(std::filesystem::exists(asm_file), "File assembly kh??ng t???n t???i");

	std::ifstream f(asm_file);
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	ASSERT(content.find("sub") != std::string::npos, "Assembly kh??ng ch???a nh??n h??m 'sub'");

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
	ASSERT(!p.has_errors(), "Parser c?? l???i");

	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(prog);
	ASSERT(!diag.has_errors(), "Semantic c?? l???i");

	CodeGen cg{&sema, "e2e_module"};
	ASSERT(cg.generate(prog), "CodeGen generate th???t b???i");

	const std::string obj_file = "e2e_kobel.obj";
	const std::string exe_file = "e2e_kobel.exe";

	std::filesystem::remove(obj_file);
	std::filesystem::remove(exe_file);

	ASSERT(cg.emit_object_file(obj_file), "Sinh file object e2e th???t b???i");
	ASSERT(CodeGen::link_executable(obj_file, exe_file), "Link executable e2e th???t b???i");
	ASSERT(std::filesystem::exists(exe_file), "File th???c thi e2e_kobel.exe kh??ng t???n t???i");

	int exit_code = std::system(".\\e2e_kobel.exe");
	ASSERT(exit_code == 42, "Exit code c???a e2e_kobel.exe mong ?????i 42, nh???n ???????c " + std::to_string(exit_code));

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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? enum th???t b???i");
	ASSERT(ir.find("504") != std::string::npos, "Thi???u h???ng s??? 504 trong IR");
	ASSERT(ir.find("505") != std::string::npos, "Thi???u h???ng s??? 505 trong IR");
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
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? Array th???t b???i");
	ASSERT(ir.find("[3 x i32]") != std::string::npos, "Thi???u ki???u [3 x i32] trong LLVM IR");
	ASSERT(ir.find("arrayidx") != std::string::npos, "Thi???u GEP arrayidx trong LLVM IR");
	ASSERT(ir.find("arraydecay") != std::string::npos, "Thi???u GEP arraydecay trong LLVM IR");
	return true;
}

bool test_codegen_struct_methods() {
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
		"    val d1: i32 = p.distance_sq();\n"
		"    p.translate(1, 2);\n"
		"    val d2: i32 = p.distance_sq();\n"
		"    return d1 + d2;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? struct methods th???t b???i");
	ASSERT(ir.find("@Point_distance_sq(") != std::string::npos, "Thi???u h??m @Point_distance_sq");
	ASSERT(ir.find("@Point_translate(") != std::string::npos, "Thi???u h??m @Point_translate");
	ASSERT(ir.find("call i32 @Point_distance_sq(") != std::string::npos, "Thi???u l???nh g???i @Point_distance_sq");
	ASSERT(ir.find("call void @Point_translate(") != std::string::npos, "Thi???u l???nh g???i @Point_translate");
	return true;
}

bool test_codegen_short_circuit_logic() {
	std::string_view code =
		"fn test_and(a: bool, b: bool): bool {\n"
		"    return a && b;\n"
		"}\n"
		"fn test_or(a: bool, b: bool): bool {\n"
		"    return a || b;\n"
		"}\n"
		"fn test_while_logic(limit: i32): i32 {\n"
		"    var i: i32 = 0;\n"
		"    var sum: i32 = 0;\n"
		"    while (i < limit && sum < 100) {\n"
		"        sum = sum + i;\n"
		"        i = i + 1;\n"
		"    }\n"
		"    return sum;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? short circuit th???t b???i");
	ASSERT(ir.find("land.rhs:") != std::string::npos, "Thi???u nh??n land.rhs");
	ASSERT(ir.find("land.merge:") != std::string::npos, "Thi???u nh??n land.merge");
	ASSERT(ir.find("lor.rhs:") != std::string::npos, "Thi???u nh??n lor.rhs");
	ASSERT(ir.find("lor.merge:") != std::string::npos, "Thi???u nh??n lor.merge");
	ASSERT(ir.find("phi i1") != std::string::npos, "Thi???u phi i1 cho short circuit");
	return true;
}

bool test_codegen_modules() {
	std::string_view code =
		"module math.calc;\n"
		"pub fn add(a: i32, b: i32): i32 {\n"
		"    return a + b;\n"
		"}\n"
		"pub const DELTA: i32 = 10;\n"
		"\n"
		"module app;\n"
		"use math.calc.add;\n"
		"use math.calc.DELTA;\n"
		"\n"
		"fn main(): i32 {\n"
		"    val res: i32 = add(20, DELTA);\n"
		"    return res;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "Ph??t sinh m?? modules th???t b???i");
	ASSERT(ir.find("@math_calc_add(") != std::string::npos, "Thi???u h??m mangled @math_calc_add");
	ASSERT(ir.find("define i32 @main()") != std::string::npos, "Thi???u h??m @main");
	ASSERT(ir.find("call i32 @math_calc_add(") != std::string::npos, "Thi???u l???nh g???i t???i @math_calc_add");
	return true;
}

bool test_codegen_str_slice() {
	std::string_view code =
		"fn main(): i32 {\n"
		"    val s: str = \"Hello, World!\";\n"
		"    val h: str = s.slice(0, 5);\n"
		"    val w: str = s.slice(7, 12);\n"
		"    val tail: str = s.slice(7);\n"
		"    val l: usz = s.len();\n"
		"    return 0;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "CodeGen for str.slice failed");
	ASSERT(ir.find("@__kobel_str_slice") != std::string::npos, "Missing @__kobel_str_slice definition");
	ASSERT(ir.find("call %str @__kobel_str_slice(") != std::string::npos, "Missing call to @__kobel_str_slice");
	return true;
}

bool test_codegen_type_size() {
	std::string_view code =
		"struct Point(x: i32, y: i32)\n"
		"enum Status { OK, ERR }\n"
		"fn main(): i32 {\n"
		"    val s_i32: usz = i32.size();\n"
		"    val s_i64: usz = i64.size();\n"
		"    val s_pt: usz = Point.size();\n"
		"    val s_str: usz = str.size();\n"
		"    val s_st: usz = Status.size();\n"
		"    return 0;\n"
		"}\n";

	std::string ir;
	ASSERT(compile_to_ir(code, ir), "CodeGen for T.size() failed");
	ASSERT(ir.find("store i64 4") != std::string::npos, "Missing 4 for i32.size()");
	ASSERT(ir.find("store i64 8") != std::string::npos, "Missing 8 for i64.size() / Point.size()");
	ASSERT(ir.find("store i64 24") != std::string::npos, "Missing 24 for str.size()");
	return true;
}

bool test_codegen_when_and_if_expr() {
	// 1. when expression, statement, and if expression
	{
		std::string_view code =
			"fn test_when(x: i32): i32 {\n"
			"    val a: i32 = when (x) {\n"
			"        1 -> 10;\n"
			"        2, 3 -> 20;\n"
			"        else -> 30;\n"
			"    };\n"
			"    return a;\n"
			"}\n"
			"fn test_if(c: bool): i32 {\n"
			"    val a: i32 = if (c) 100 else -100;\n"
			"    val b: i32 = if (c) { 200 } else { -200 };\n"
			"    return a + b;\n"
			"}\n"
			"fn test_when_stmt(x: i32): i32 {\n"
			"    var res: i32 = 0;\n"
			"    when (x) {\n"
			"        1 -> res = 10;\n"
			"        2, 3 -> { res = 20; }\n"
			"        else -> res = 30;\n"
			"    }\n"
			"    return res;\n"
			"}\n"
			"fn test_if_unbraced(x: i32): i32 {\n"
			"    if (x > 0) return 1; else return 0;\n"
			"}\n";

		std::string ir;
		ASSERT(compile_to_ir(code, ir), "CodeGen for when and if expr failed");
		ASSERT(ir.find("phi i32") != std::string::npos, "Missing phi node for when/if expr");
		ASSERT(ir.find("when_arm_body") != std::string::npos, "Missing when_arm_body basic block");
		ASSERT(ir.find("ifexpr_then") != std::string::npos, "Missing ifexpr_then basic block");
	}

	return true;
}

int main() {
	int passed = 0;
	int total = 18;

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
	if (test_codegen_struct_methods()) {
		std::cout << "[PASS] test_codegen_struct_methods\n";
		passed++;
	}
	if (test_codegen_short_circuit_logic()) {
		std::cout << "[PASS] test_codegen_short_circuit_logic\n";
		passed++;
	}
	if (test_codegen_modules()) {
		std::cout << "[PASS] test_codegen_modules\n";
		passed++;
	}
	if (test_codegen_str_slice()) {
		std::cout << "[PASS] test_codegen_str_slice\n";
		passed++;
	}
	if (test_codegen_type_size()) {
		std::cout << "[PASS] test_codegen_type_size\n";
		passed++;
	}
	if (test_codegen_when_and_if_expr()) {
		std::cout << "[PASS] test_codegen_when_and_if_expr\n";
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

