#include <iostream>
#include <string_view>

import token;
import lexer;
import ast;
import parser;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool test_parse_expressions() {
	// Kiểm tra độ ưu tiên: 1 + 2 * 3
	std::string_view code = "1 + 2 * 3";
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto expr = p.parse_expression();

	ASSERT(expr != nullptr, "Biểu thức 1 + 2 * 3 parse thất bại");
	ASSERT(isa<BinaryExpr>(expr.get()), "Node gốc phải là BinaryExpr (+)");
	auto bin = as<BinaryExpr>(expr.get());
	ASSERT(bin->op == TokenType::PLUS, "Toán tử gốc phải là PLUS (+)");
	ASSERT(isa<LiteralExpr>(bin->left.get()), "Vế trái phải là LiteralExpr (1)");
	ASSERT(isa<BinaryExpr>(bin->right.get()), "Vế phải phải là BinaryExpr (2 * 3)");

	auto right_bin = as<BinaryExpr>(bin->right.get());
	ASSERT(right_bin->op == TokenType::STAR, "Toán tử nhánh phải phải là STAR (*)");

	// Kiểm tra gọi hàm, truy cập trường và chỉ mục: foo(p.x, arr[0])
	std::string_view code2 = "foo(p.x, arr[0])";
	Lexer lex2{code2};
	Parser p2{lex2.tokenize()};
	auto call_expr = p2.parse_expression();

	ASSERT(call_expr != nullptr, "Parse call expression thất bại");
	ASSERT(isa<CallExpr>(call_expr.get()), "Phải là CallExpr");
	auto call = as<CallExpr>(call_expr.get());
	ASSERT(call->args.size() == 2, "Số lượng đối số phải là 2");
	ASSERT(isa<MemberExpr>(call->args[0].get()), "Đối số 1 phải là MemberExpr (p.x)");
	ASSERT(isa<IndexExpr>(call->args[1].get()), "Đối số 2 phải là IndexExpr (arr[0])");

	// Kiểm tra ép kiểu: x as i32
	std::string_view code3 = "x as i32";
	Lexer lex3{code3};
	Parser p3{lex3.tokenize()};
	auto cast_expr = p3.parse_expression();
	ASSERT(isa<CastExpr>(cast_expr.get()), "Phải là CastExpr");

	// Kiểm tra phép gán: a = b = 10
	std::string_view code4 = "a = 10";
	Lexer lex4{code4};
	Parser p4{lex4.tokenize()};
	auto assign_expr = p4.parse_expression();
	ASSERT(isa<AssignExpr>(assign_expr.get()), "Phải là AssignExpr");

	return true;
}

bool test_parse_statements() {
	std::string_view code = 
		"val x: i32 = 10;\n"
		"var y = x + 1;\n"
		"if (x > 0) {\n"
		"    return x;\n"
		"} else {\n"
		"    return 0;\n"
		"}\n"
		"while (y > 0) {\n"
		"    y = y - 1;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};

	// 1. val x: i32 = 10;
	auto s1 = p.parse_statement();
	ASSERT(isa<VarDeclStmt>(s1.get()), "s1 phải là VarDeclStmt (val)");
	auto v1 = as<VarDeclStmt>(s1.get());
	ASSERT(!v1->is_mut && v1->name == "x", "v1 phải là val x");
	ASSERT(v1->type_annotation != nullptr && isa<NamedType>(v1->type_annotation.get()), "v1 phải có type i32");

	// 2. var y = x + 1;
	auto s2 = p.parse_statement();
	ASSERT(isa<VarDeclStmt>(s2.get()), "s2 phải là VarDeclStmt (var)");
	auto v2 = as<VarDeclStmt>(s2.get());
	ASSERT(v2->is_mut && v2->name == "y", "v2 phải là var y");
	ASSERT(v2->type_annotation == nullptr, "v2 không ghi type tường minh");

	// 3. if (x > 0) { ... } else { ... }
	auto s3 = p.parse_statement();
	ASSERT(isa<IfStmt>(s3.get()), "s3 phải là IfStmt");
	auto if_stmt = as<IfStmt>(s3.get());
	ASSERT(if_stmt->then_branch != nullptr, "then_branch không được null");
	ASSERT(if_stmt->else_branch != nullptr, "else_branch không được null");

	// 4. while (y > 0) { ... }
	auto s4 = p.parse_statement();
	ASSERT(isa<WhileStmt>(s4.get()), "s4 phải là WhileStmt");

	return true;
}

bool test_parse_functions() {
	std::string_view code = 
		"fn add(a: i32, b: i32): i32 {\n"
		"    return a + b;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parse hàm không được có lỗi cú pháp");
	ASSERT(prog->declarations.size() == 1, "Chương trình phải chứa 1 khai báo");
	ASSERT(isa<FnDecl>(prog->declarations[0].get()), "Khai báo phải là FnDecl");

	auto fn = as<FnDecl>(prog->declarations[0].get());
	ASSERT(fn->name == "add", "Tên hàm phải là 'add'");
	ASSERT(fn->params.size() == 2, "Hàm phải có 2 tham số");
	ASSERT(fn->params[0].name == "a" && fn->params[1].name == "b", "Tên tham số a và b");
	ASSERT(fn->return_type != nullptr, "Kiểu trả về không được null");
	ASSERT(fn->body != nullptr, "Thân hàm không được null");
	ASSERT(fn->body->statements.size() == 1, "Thân hàm phải chứa 1 lệnh (return)");

	return true;
}

bool test_parse_structs() {
	std::string_view code = "struct Point(x: i32, y: i32);";
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parse struct không được có lỗi cú pháp");
	ASSERT(prog->declarations.size() == 1, "Chương trình phải chứa 1 khai báo");
	ASSERT(isa<StructDecl>(prog->declarations[0].get()), "Khai báo phải là StructDecl");

	auto st = as<StructDecl>(prog->declarations[0].get());
	ASSERT(st->name == "Point", "Tên struct phải là 'Point'");
	ASSERT(st->fields.size() == 2, "Struct phải có 2 trường");
	ASSERT(st->fields[0].name == "x" && st->fields[1].name == "y", "Tên trường x và y");

	return true;
}

bool test_parse_extern_and_const() {
	std::string_view code = 
		"const BUFFER_SIZE: i32 = 1024;\n"
		"extern \"libc\" {\n"
		"    fn printf(fmt: *char): i32;\n"
		"    fn malloc(size: usz): *u8;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parse extern & const không được có lỗi cú pháp");
	ASSERT(prog->declarations.size() == 2, "Chương trình phải chứa 2 khai báo");

	ASSERT(isa<ConstDecl>(prog->declarations[0].get()), "Khai báo 1 phải là ConstDecl");
	auto c = as<ConstDecl>(prog->declarations[0].get());
	ASSERT(c->name == "BUFFER_SIZE", "Tên const phải là BUFFER_SIZE");

	ASSERT(isa<ExternBlock>(prog->declarations[1].get()), "Khai báo 2 phải là ExternBlock");
	auto ext = as<ExternBlock>(prog->declarations[1].get());
	ASSERT(ext->abi == "\"libc\"", "ABI phải là libc");
	ASSERT(ext->declarations.size() == 2, "Khối extern phải chứa 2 hàm");
	ASSERT(ext->declarations[0]->name == "printf", "Hàm 1 phải là printf");
	ASSERT(ext->declarations[1]->name == "malloc", "Hàm 2 phải là malloc");

	return true;
}

bool test_error_recovery() {
	// Cú pháp lỗi ở dòng 1 (thiếu tên biến), dòng 2 hợp lệ
	std::string_view code = 
		"val = 10;\n"
		"fn valid_fn(): i32 { return 42; }\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(p.has_errors(), "Parser phải phát hiện lỗi ở dòng 1");
	// Nhờ hàm synchronize(), dòng 2 vẫn được phục hồi và parse thành công!
	ASSERT(prog->declarations.size() == 1, "Parser phải phục hồi và parse được valid_fn");
	ASSERT(isa<FnDecl>(prog->declarations[0].get()), "Khai báo phục hồi phải là FnDecl");

	return true;
}

bool test_parse_enum() {
	std::string_view code =
		"enum Status {\n"
		"    OK,\n"
		"    ERROR = 504,\n"
		"    UNKNOWN,\n"
		"}\n"
		"enum Priority : u8 {\n"
		"    LOW = 1,\n"
		"    HIGH = 10,\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser không được có lỗi khi parse enum");
	ASSERT(prog->declarations.size() == 2, "Phải parse được 2 declarations");
	ASSERT(isa<EnumDecl>(prog->declarations[0].get()), "decl 0 phải là EnumDecl");
	ASSERT(isa<EnumDecl>(prog->declarations[1].get()), "decl 1 phải là EnumDecl");

	auto* e0 = as<EnumDecl>(prog->declarations[0].get());
	ASSERT(e0->name == "Status", "Tên enum 0 phải là Status");
	ASSERT(e0->underlying_type == nullptr, "Kiểu cơ sở của Status phải là nullptr (mặc định)");
	ASSERT(e0->members.size() == 3, "Status phải có 3 members");
	ASSERT(e0->members[0].name == "OK", "Member 0 là OK");
	ASSERT(e0->members[0].value == nullptr, "Member OK không có gán giá trị");
	ASSERT(e0->members[1].name == "ERROR", "Member 1 là ERROR");
	ASSERT(e0->members[1].value != nullptr, "Member ERROR có gán giá trị");

	auto* e1 = as<EnumDecl>(prog->declarations[1].get());
	ASSERT(e1->name == "Priority", "Tên enum 1 phải là Priority");
	ASSERT(e1->underlying_type != nullptr, "Kiểu cơ sở của Priority không được null");
	ASSERT(e1->members.size() == 2, "Priority phải có 2 members");

	return true;
}

bool test_parse_array() {
	std::string_view code =
		"fn test(): void {\n"
		"    val a: Array<i32> = [1, 2, 3];\n"
		"    val b: Array<u8>(4) = [1, 2, 3, 4];\n"
		"    val c: i32 = a[0];\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser không được có lỗi khi parse array");
	ASSERT(prog->declarations.size() == 1, "Phải parse được 1 hàm");
	auto* fn = as<FnDecl>(prog->declarations[0].get());
	ASSERT(fn->body->statements.size() == 3, "Thân hàm phải có 3 câu lệnh");

	// val a: Array<i32> = [1, 2, 3];
	auto* s0 = as<VarDeclStmt>(fn->body->statements[0].get());
	ASSERT(isa<ArrayType>(s0->type_annotation.get()), "s0 type phải là ArrayType");
	auto* arr_ty_a = as<ArrayType>(s0->type_annotation.get());
	ASSERT(arr_ty_a->size == 0, "ArrayType size của a phải là 0 (suy luận)");
	ASSERT(isa<ArrayLiteralExpr>(s0->initializer.get()), "s0 init phải là ArrayLiteralExpr");
	auto* arr_lit_a = as<ArrayLiteralExpr>(s0->initializer.get());
	ASSERT(arr_lit_a->elements.size() == 3, "a có 3 phần tử");

	// val b: Array<u8>(4) = [1, 2, 3, 4];
	auto* s1 = as<VarDeclStmt>(fn->body->statements[1].get());
	auto* arr_ty_b = as<ArrayType>(s1->type_annotation.get());
	ASSERT(arr_ty_b->size == 4, "ArrayType size của b phải là 4");

	// val c: i32 = a[0];
	auto* s2 = as<VarDeclStmt>(fn->body->statements[2].get());
	ASSERT(isa<IndexExpr>(s2->initializer.get()), "s2 init phải là IndexExpr");

	return true;
}

bool test_parse_struct_methods() {
	std::string_view code =
		"struct Point(x: i32, y: i32) {\n"
		"    fn distance_sq(val self): i32 => self.x * self.x + self.y * self.y;\n"
		"    fn translate(var self, dx: i32, dy: i32): void {\n"
		"        self.x = self.x + dx;\n"
		"        self.y = self.y + dy;\n"
		"    }\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser không được có lỗi khi parse struct methods");
	ASSERT(prog->declarations.size() == 1, "Phải parse được 1 StructDecl");
	auto* st = as<StructDecl>(prog->declarations[0].get());
	ASSERT(st->name == "Point", "Tên struct phải là Point");
	ASSERT(st->fields.size() == 2, "Struct có 2 fields");
	ASSERT(st->methods.size() == 2, "Struct có 2 methods");

	// Method 1: distance_sq
	const auto& m0 = st->methods[0];
	ASSERT(m0->name == "distance_sq", "Method 0 là distance_sq");
	ASSERT(m0->params.size() == 1, "Method 0 có 1 param");
	ASSERT(m0->params[0].name == "self", "Param 0 là self");
	ASSERT(m0->params[0].has_val && !m0->params[0].is_mut, "Param 0 là val self");
	ASSERT(m0->body != nullptr, "Method 0 có thân hàm (=> expr)");

	// Method 2: translate
	const auto& m1 = st->methods[1];
	ASSERT(m1->name == "translate", "Method 1 là translate");
	ASSERT(m1->params.size() == 3, "Method 1 có 3 params");
	ASSERT(m1->params[0].name == "self" && m1->params[0].is_mut, "Param 0 là var self");

	return true;
}

bool test_parse_logical_expressions() {
	std::string_view code =
		"fn test_logic(): void {\n"
		"    val r1: bool = a && b || c && !d;\n"
		"    val r2: bool = (a || b) && c;\n"
		"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser không được có lỗi khi parse logical expressions");
	ASSERT(prog->declarations.size() == 1, "Phải parse được 1 hàm");
	auto* fn = as<FnDecl>(prog->declarations[0].get());

	// Statement 1: val r1: bool = a && b || c && !d;
	// Precedence: || is top-level binary op
	auto* s0 = as<VarDeclStmt>(fn->body->statements[0].get());
	ASSERT(isa<BinaryExpr>(s0->initializer.get()), "Init của r1 phải là BinaryExpr");
	auto* top_or = as<BinaryExpr>(s0->initializer.get());
	ASSERT(top_or->op == TokenType::OR_OR, "Top op của r1 phải là ||");
	ASSERT(isa<BinaryExpr>(top_or->left.get()), "Vế trái của || phải là BinaryExpr");
	ASSERT(as<BinaryExpr>(top_or->left.get())->op == TokenType::AND_AND, "Vế trái phải là &&");
	ASSERT(isa<BinaryExpr>(top_or->right.get()), "Vế phải của || phải là BinaryExpr");
	auto* right_and = as<BinaryExpr>(top_or->right.get());
	ASSERT(right_and->op == TokenType::AND_AND, "Vế phải phải là &&");
	ASSERT(isa<UnaryExpr>(right_and->right.get()), "Vế phải của && phải là UnaryExpr (!d)");
	ASSERT(as<UnaryExpr>(right_and->right.get())->op == TokenType::BANG, "Unary op phải là !");

	// Statement 2: val r2: bool = (a || b) && c;
	auto* s1 = as<VarDeclStmt>(fn->body->statements[1].get());
	ASSERT(isa<BinaryExpr>(s1->initializer.get()), "Init của r2 phải là BinaryExpr");
	auto* top_and = as<BinaryExpr>(s1->initializer.get());
	ASSERT(top_and->op == TokenType::AND_AND, "Top op của r2 phải là &&");
	ASSERT(isa<GroupExpr>(top_and->left.get()), "Vế trái của && phải là GroupExpr");

	return true;
}

bool test_parse_module_and_use() {
	std::string_view code =
		"module math.geometry.point;\n"
		"\n"
		"use math.calc.add;\n"
		"use graphics.Point as GPoint;\n"
		"use std.collections.*;\n"
		"\n"
		"pub fn compute(): i32 {\n"
		"    return 42;\n"
		"}\n"
		"\n"
		"pub struct Vector(x: i32, y: i32);\n"
		"pub enum Color { RED, GREEN, BLUE }\n"
		"pub const MAX: i32 = 100;\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser không được có lỗi khi parse module, use, pub");
	ASSERT(prog->declarations.size() == 8, "Phải parse được 8 declarations");

	// 1. module math.geometry.point;
	ASSERT(isa<ModuleDecl>(prog->declarations[0].get()), "decl 0 phải là ModuleDecl");
	auto* mod = as<ModuleDecl>(prog->declarations[0].get());
	ASSERT(mod->path.size() == 3, "Module path phải có 3 phần: math, geometry, point");
	ASSERT(mod->path[0] == "math", "Phần 0 là math");
	ASSERT(mod->path[1] == "geometry", "Phần 1 là geometry");
	ASSERT(mod->path[2] == "point", "Phần 2 là point");

	// 2. use math.calc.add;
	ASSERT(isa<UseDecl>(prog->declarations[1].get()), "decl 1 phải là UseDecl");
	auto* u1 = as<UseDecl>(prog->declarations[1].get());
	ASSERT(u1->path.size() == 2 && u1->path[0] == "math" && u1->path[1] == "calc", "u1 path là math.calc");
	ASSERT(u1->symbol_name == "add", "u1 symbol_name là add");
	ASSERT(u1->alias.empty(), "u1 không có alias");
	ASSERT(!u1->is_wildcard, "u1 không phải wildcard");

	// 3. use graphics.Point as GPoint;
	ASSERT(isa<UseDecl>(prog->declarations[2].get()), "decl 2 phải là UseDecl");
	auto* u2 = as<UseDecl>(prog->declarations[2].get());
	ASSERT(u2->path.size() == 1 && u2->path[0] == "graphics", "u2 path là graphics");
	ASSERT(u2->symbol_name == "Point", "u2 symbol_name là Point");
	ASSERT(u2->alias == "GPoint", "u2 alias là GPoint");
	ASSERT(!u2->is_wildcard, "u2 không phải wildcard");

	// 4. use std.collections.*;
	ASSERT(isa<UseDecl>(prog->declarations[3].get()), "decl 3 phải là UseDecl");
	auto* u3 = as<UseDecl>(prog->declarations[3].get());
	ASSERT(u3->path.size() == 2 && u3->path[0] == "std" && u3->path[1] == "collections", "u3 path là std.collections");
	ASSERT(u3->is_wildcard, "u3 phải là wildcard (*)");

	// 5. pub fn compute()
	ASSERT(isa<FnDecl>(prog->declarations[4].get()), "decl 4 phải là FnDecl");
	auto* fn = as<FnDecl>(prog->declarations[4].get());
	ASSERT(fn->name == "compute", "Tên hàm là compute");
	ASSERT(fn->is_pub, "Hàm compute phải có cờ is_pub == true");

	// 6. pub struct Vector
	ASSERT(isa<StructDecl>(prog->declarations[5].get()), "decl 5 phải là StructDecl");
	auto* st = as<StructDecl>(prog->declarations[5].get());
	ASSERT(st->name == "Vector", "Tên struct là Vector");
	ASSERT(st->is_pub, "Struct Vector phải có cờ is_pub == true");

	// 7. pub enum Color
	ASSERT(isa<EnumDecl>(prog->declarations[6].get()), "decl 6 phải là EnumDecl");
	auto* en = as<EnumDecl>(prog->declarations[6].get());
	ASSERT(en->name == "Color", "Tên enum là Color");
	ASSERT(en->is_pub, "Enum Color phải có cờ is_pub == true");

	// 8. pub const MAX
	ASSERT(isa<ConstDecl>(prog->declarations[7].get()), "decl 7 phải là ConstDecl");
	auto* cn = as<ConstDecl>(prog->declarations[7].get());
	ASSERT(cn->name == "MAX", "Tên const là MAX");
	ASSERT(cn->is_pub, "Const MAX phải có cờ is_pub == true");

	return true;
}

bool test_empty_parser_safety() {
	Parser p{{}};
	ASSERT(p.is_end(), "Empty parser must report is_end");
	Token prev = p.previous();
	ASSERT(prev.type == TokenType::END_OF_FILE, "Previous on empty parser must return EOF");
	Token pk = p.peek();
	ASSERT(pk.type == TokenType::END_OF_FILE, "Peek on empty parser must return EOF");
	return true;
}

int main() {
	std::cout << "[RUNNING] Parser tests..." << std::endl;

	if (!test_empty_parser_safety()) return 1;
	std::cout << "  [PASS] test_empty_parser_safety" << std::endl;

	if (!test_parse_expressions()) return 1;
	std::cout << "  [PASS] test_parse_expressions" << std::endl;

	if (!test_parse_statements()) return 1;
	std::cout << "  [PASS] test_parse_statements" << std::endl;

	if (!test_parse_functions()) return 1;
	std::cout << "  [PASS] test_parse_functions" << std::endl;

	if (!test_parse_structs()) return 1;
	std::cout << "  [PASS] test_parse_structs" << std::endl;

	if (!test_parse_enum()) return 1;
	std::cout << "  [PASS] test_parse_enum" << std::endl;

	if (!test_parse_array()) return 1;
	std::cout << "  [PASS] test_parse_array" << std::endl;

	if (!test_parse_struct_methods()) return 1;
	std::cout << "  [PASS] test_parse_struct_methods" << std::endl;

	if (!test_parse_logical_expressions()) return 1;
	std::cout << "  [PASS] test_parse_logical_expressions" << std::endl;

	if (!test_parse_module_and_use()) return 1;
	std::cout << "  [PASS] test_parse_module_and_use" << std::endl;

	if (!test_parse_extern_and_const()) return 1;
	std::cout << "  [PASS] test_parse_extern_and_const" << std::endl;

	if (!test_error_recovery()) return 1;
	std::cout << "  [PASS] test_error_recovery" << std::endl;

	std::cout << "[ALL PASSED] Parser tests passed successfully!" << std::endl;
	return 0;
}


