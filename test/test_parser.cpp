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
	// Operator precedence test: 1 + 2 * 3
	std::string_view code = "1 + 2 * 3";
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto expr = p.parse_expression();

	ASSERT(expr != nullptr, "Failed to parse expression 1 + 2 * 3");
	ASSERT(isa<BinaryExpr>(expr), "Root node must be BinaryExpr (+)");
	auto bin = as<BinaryExpr>(expr);
	ASSERT(bin->op == TokenType::PLUS, "Root operator must be PLUS (+)");
	ASSERT(isa<LiteralExpr>(bin->left), "Left operand must be LiteralExpr (1)");
	ASSERT(isa<BinaryExpr>(bin->right), "Right operand must be BinaryExpr (2 * 3)");

	auto right_bin = as<BinaryExpr>(bin->right);
	ASSERT(right_bin->op == TokenType::STAR, "Right branch operator must be STAR (*)");

	// Function call, member access, and index access: foo(p.x, arr[0])
	std::string_view code2 = "foo(p.x, arr[0])";
	Lexer lex2{code2};
	Parser p2{lex2.tokenize()};
	auto call_expr = p2.parse_expression();

	ASSERT(call_expr != nullptr, "Failed to parse call expression");
	ASSERT(isa<CallExpr>(call_expr), "Must be CallExpr");
	auto call = as<CallExpr>(call_expr);
	ASSERT(call->args.size() == 2, "Argument count must be 2");
	ASSERT(isa<MemberExpr>(call->args[0]), "Argument 1 must be MemberExpr (p.x)");
	ASSERT(isa<IndexExpr>(call->args[1]), "Argument 2 must be IndexExpr (arr[0])");

	// Type cast: x as i32
	std::string_view code3 = "x as i32";
	Lexer lex3{code3};
	Parser p3{lex3.tokenize()};
	auto cast_expr = p3.parse_expression();
	ASSERT(isa<CastExpr>(cast_expr), "Must be CastExpr");

	// Assignment: a = 10
	std::string_view code4 = "a = 10";
	Lexer lex4{code4};
	Parser p4{lex4.tokenize()};
	auto assign_expr = p4.parse_expression();
	ASSERT(isa<AssignExpr>(assign_expr), "Must be AssignExpr");

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
	ASSERT(isa<VarDeclStmt>(s1), "s1 must be VarDeclStmt (val)");
	auto v1 = as<VarDeclStmt>(s1);
	ASSERT(!v1->is_mut && v1->name == "x", "v1 must be val x");
	ASSERT(v1->type_annotation != nullptr && isa<NamedType>(v1->type_annotation), "v1 must have type i32");

	// 2. var y = x + 1;
	auto s2 = p.parse_statement();
	ASSERT(isa<VarDeclStmt>(s2), "s2 must be VarDeclStmt (var)");
	auto v2 = as<VarDeclStmt>(s2);
	ASSERT(v2->is_mut && v2->name == "y", "v2 must be var y");
	ASSERT(v2->type_annotation == nullptr, "v2 has no explicit type annotation");

	// 3. if (x > 0) { ... } else { ... }
	auto s3 = p.parse_statement();
	ASSERT(isa<IfStmt>(s3), "s3 must be IfStmt");
	auto if_stmt = as<IfStmt>(s3);
	ASSERT(if_stmt->then_branch != nullptr, "then_branch must not be null");
	ASSERT(if_stmt->else_branch != nullptr, "else_branch must not be null");

	// 4. while (y > 0) { ... }
	auto s4 = p.parse_statement();
	ASSERT(isa<WhileStmt>(s4), "s4 must be WhileStmt");

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

	ASSERT(!p.has_errors(), "Parsing function must not produce syntax errors");
	ASSERT(prog->declarations.size() == 1, "Program must contain 1 declaration");
	ASSERT(isa<FnDecl>(prog->declarations[0]), "Declaration must be FnDecl");

	auto fn = as<FnDecl>(prog->declarations[0]);
	ASSERT(fn->name == "add", "Function name must be 'add'");
	ASSERT(fn->params.size() == 2, "Function must have 2 parameters");
	ASSERT(fn->params[0].name == "a" && fn->params[1].name == "b", "Parameter names must be a and b");
	ASSERT(fn->return_type != nullptr, "Return type must not be null");
	ASSERT(fn->body != nullptr, "Function body must not be null");
	ASSERT(fn->body->statements.size() == 1, "Function body must contain 1 statement (return)");

	return true;
}

bool test_parse_structs() {
	std::string_view code = "struct Point { x: i32, y: i32 }";
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parse struct must not produce syntax errors");
	ASSERT(prog->declarations.size() == 1, "Program must contain 1 declaration");
	ASSERT(isa<StructDecl>(prog->declarations[0]), "Declaration must be StructDecl");

	auto st = as<StructDecl>(prog->declarations[0]);
	ASSERT(st->name == "Point", "Struct name must be 'Point'");
	ASSERT(st->fields.size() == 2, "Struct must have 2 fields");
	ASSERT(st->fields[0].name == "x" && st->fields[1].name == "y", "Field names must be x and y");

	return true;
}

bool test_parse_new_struct_and_impl() {
	std::string_view code =
			"struct List<T> {\n"
			"    pub data: &T,\n"
			"    len: usz,\n"
			"    cap: usz\n"
			"}\n"
			"\n"
			"impl List<T> {\n"
			"    fn add(var self, value: T) {\n"
			"    }\n"
			"\n"
			"    fn free(self) {\n"
			"    }\n"
			"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parse new struct & impl must not have errors");
	ASSERT(prog->declarations.size() == 2, "Program must contain 2 declarations (StructDecl and ImplDecl)");
	ASSERT(isa<StructDecl>(prog->declarations[0]), "First decl must be StructDecl");
	ASSERT(isa<ImplDecl>(prog->declarations[1]), "Second decl must be ImplDecl");

	auto st = as<StructDecl>(prog->declarations[0]);
	ASSERT(st->name == "List", "Struct name must be 'List'");
	ASSERT(st->type_params.size() == 1 && st->type_params[0].name == "T", "Generic type param must be T");
	ASSERT(st->fields.size() == 3, "List struct must have 3 fields");
	ASSERT(st->fields[0].name == "data" && st->fields[0].is_pub, "data field must be pub");
	ASSERT(st->fields[1].name == "len" && !st->fields[1].is_pub, "len field must not be pub");
	ASSERT(st->fields[2].name == "cap" && !st->fields[2].is_pub, "cap field must not be pub");

	auto imp = as<ImplDecl>(prog->declarations[1]);
	ASSERT(imp->struct_name == "List", "Impl struct name must be 'List'");
	ASSERT(imp->methods.size() == 2, "Impl must contain 2 methods");
	ASSERT(imp->methods[0]->name == "add", "First method must be 'add'");
	ASSERT(imp->methods[1]->name == "free", "Second method must be 'free'");

	// Test error when methods are placed inside struct syntax
	{
		std::string_view code_struct_method =
				"struct Widget {\n"
				"    pub id: i32,\n"
				"    pub fn render(val self): void {}\n"
				"}\n";
		Lexer lex_sm{code_struct_method};
		Parser p_sm{lex_sm.tokenize()};
		p_sm.parse_program();
		ASSERT(p_sm.has_errors(), "Methods inside struct must report error");
	}

	// Test syntax error for invalid content inside impl
	{
		std::string_view bad_impl =
				"impl List<T> {\n"
				"    var x: i32;\n"
				"}\n";
		Lexer lex_bad{bad_impl};
		Parser p_bad{lex_bad.tokenize()};
		p_bad.parse_program();
		ASSERT(p_bad.has_errors(), "Invalid statement in impl block must report error");
	}

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

	ASSERT(!p.has_errors(), "Parse extern & const must not produce syntax errors");
	ASSERT(prog->declarations.size() == 2, "Program must contain 2 declarations");

	ASSERT(isa<ConstDecl>(prog->declarations[0]), "Declaration 1 must be ConstDecl");
	auto c = as<ConstDecl>(prog->declarations[0]);
	ASSERT(c->name == "BUFFER_SIZE", "Const name must be BUFFER_SIZE");

	ASSERT(isa<ExternBlock>(prog->declarations[1]), "Declaration 2 must be ExternBlock");
	auto ext = as<ExternBlock>(prog->declarations[1]);
	ASSERT(ext->abi == "\"libc\"", "ABI must be libc");
	ASSERT(ext->declarations.size() == 2, "Extern block must contain 2 functions");
	ASSERT(ext->declarations[0]->name == "printf", "Function 1 must be printf");
	ASSERT(ext->declarations[1]->name == "malloc", "Function 2 must be malloc");

	return true;
}

bool test_error_recovery() {
	// Syntax error on line 1 (missing variable name), line 2 is valid
	std::string_view code =
			"val = 10;\n"
			"fn valid_fn(): i32 { return 42; }\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(p.has_errors(), "Parser must report error on line 1");
	// Via synchronize(), line 2 is recovered and parsed successfully
	ASSERT(prog->declarations.size() == 1, "Parser must recover and parse valid_fn");
	ASSERT(isa<FnDecl>(prog->declarations[0]), "Recovered declaration must be FnDecl");

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

	ASSERT(!p.has_errors(), "Parser must not have errors when parsing enum");
	ASSERT(prog->declarations.size() == 2, "Must parse 2 declarations");
	ASSERT(isa<EnumDecl>(prog->declarations[0]), "decl 0 must be EnumDecl");
	ASSERT(isa<EnumDecl>(prog->declarations[1]), "decl 1 must be EnumDecl");

	auto *e0 = as<EnumDecl>(prog->declarations[0]);
	ASSERT(e0->name == "Status", "Enum 0 name must be Status");
	ASSERT(e0->underlying_type == nullptr, "Status underlying type must be nullptr (default)");
	ASSERT(e0->members.size() == 3, "Status must have 3 members");
	ASSERT(e0->members[0].name == "OK", "Member 0 must be OK");
	ASSERT(e0->members[0].value == nullptr, "Member OK has no assigned value");
	ASSERT(e0->members[1].name == "ERROR", "Member 1 must be ERROR");
	ASSERT(e0->members[1].value != nullptr, "Member ERROR has an assigned value");

	auto *e1 = as<EnumDecl>(prog->declarations[1]);
	ASSERT(e1->name == "Priority", "Enum 1 name must be Priority");
	ASSERT(e1->underlying_type != nullptr, "Priority underlying type must not be null");
	ASSERT(e1->members.size() == 2, "Priority must have 2 members");

	return true;
}

bool test_parse_array() {
	std::string_view code =
			"fn test(): void {\n"
			"    val a: Array<i32> = [1, 2, 3];\n"
			"    val b: Array<u8>(4) = [1, 2, 3, 4];\n"
			"    val c: i32 = a[0];\n"
			"    val d: Array<i32>(1_000) = [];\n"
			"    val e: Array<i32>(0x10) = [];\n"
			"    val f: Array<i32>(10UZ) = [];\n"
			"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser should have no errors parsing arrays");
	ASSERT(prog->declarations.size() == 1, "Must parse 1 function");
	auto *fn = as<FnDecl>(prog->declarations[0]);
	ASSERT(fn->body->statements.size() == 6, "Function body should have 6 statements");

	// val a: Array<i32> = [1, 2, 3];
	auto *s0 = as<VarDeclStmt>(fn->body->statements[0]);
	ASSERT(isa<ArrayType>(s0->type_annotation), "s0 type must be ArrayType");
	auto *arr_ty_a = as<ArrayType>(s0->type_annotation);
	ASSERT(arr_ty_a->size == 0, "ArrayType size of a must be 0");
	ASSERT(isa<ArrayLiteralExpr>(s0->initializer), "s0 init must be ArrayLiteralExpr");
	auto *arr_lit_a = as<ArrayLiteralExpr>(s0->initializer);
	ASSERT(arr_lit_a->elements.size() == 3, "a has 3 elements");

	// val b: Array<u8>(4) = [1, 2, 3, 4];
	auto *s1 = as<VarDeclStmt>(fn->body->statements[1]);
	auto *arr_ty_b = as<ArrayType>(s1->type_annotation);
	ASSERT(arr_ty_b->size == 4, "ArrayType size of b must be 4");

	// val c: i32 = a[0];
	auto *s2 = as<VarDeclStmt>(fn->body->statements[2]);
	ASSERT(isa<IndexExpr>(s2->initializer), "s2 init must be IndexExpr");

	// val d: Array<i32>(1_000) = [];
	auto *s3 = as<VarDeclStmt>(fn->body->statements[3]);
	auto *arr_ty_d = as<ArrayType>(s3->type_annotation);
	ASSERT(arr_ty_d->size == 1000, "ArrayType size of d must be 1000");

	// val e: Array<i32>(0x10) = [];
	auto *s4 = as<VarDeclStmt>(fn->body->statements[4]);
	auto *arr_ty_e = as<ArrayType>(s4->type_annotation);
	ASSERT(arr_ty_e->size == 16, "ArrayType size of e must be 16");

	// val f: Array<i32>(10UZ) = [];
	auto *s5 = as<VarDeclStmt>(fn->body->statements[5]);
	auto *arr_ty_f = as<ArrayType>(s5->type_annotation);
	ASSERT(arr_ty_f->size == 10, "ArrayType size of f must be 10");

	// Test invalid array size numbers
	{
		std::string_view bad_code = "fn test(): void { val x: Array<i32>(10.5) = []; }\n";
		Lexer bad_lex{bad_code};
		Parser bad_p{bad_lex.tokenize()};
		bad_p.parse_program();
		ASSERT(bad_p.has_errors(), "Parser must reject float array size");
	}
	{
		std::string_view bad_code = "fn test(): void { val x: Array<i32>(10F) = []; }\n";
		Lexer bad_lex{bad_code};
		Parser bad_p{bad_lex.tokenize()};
		bad_p.parse_program();
		ASSERT(bad_p.has_errors(), "Parser must reject float suffix F in array size");
	}
	{
		std::string_view bad_code = "fn test(): void { val x: Array<i32>(10D) = []; }\n";
		Lexer bad_lex{bad_code};
		Parser bad_p{bad_lex.tokenize()};
		bad_p.parse_program();
		ASSERT(bad_p.has_errors(), "Parser must reject double suffix D in array size");
	}

	return true;
}

bool test_parse_struct_methods() {
	std::string_view code =
			"struct Point {\n"
			"    pub x: i32,\n"
			"    pub y: i32\n"
			"}\n"
			"impl Point {\n"
			"    fn distance_sq(val self): i32 => self.x * self.x + self.y * self.y;\n"
			"    fn translate(var self, dx: i32, dy: i32): void {\n"
			"        self.x = self.x + dx;\n"
			"        self.y = self.y + dy;\n"
			"    }\n"
			"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser must not have errors when parsing struct methods");
	ASSERT(prog->declarations.size() == 2, "Must parse 1 StructDecl and 1 ImplDecl");
	auto *st = as<StructDecl>(prog->declarations[0]);
	ASSERT(st->name == "Point", "Struct name must be Point");
	ASSERT(st->fields.size() == 2, "Struct has 2 fields");
	auto *imp = as<ImplDecl>(prog->declarations[1]);
	ASSERT(imp->struct_name == "Point", "Impl struct name must be Point");
	ASSERT(imp->methods.size() == 2, "Impl has 2 methods");

	// Method 1: distance_sq
	const auto &m0 = imp->methods[0];
	ASSERT(m0->name == "distance_sq", "Method 0 is distance_sq");
	ASSERT(m0->params.size() == 1, "Method 0 has 1 param");
	ASSERT(m0->params[0].name == "self", "Param 0 is self");
	ASSERT(m0->params[0].has_val && !m0->params[0].is_mut, "Param 0 is val self");
	ASSERT(m0->body != nullptr, "Method 0 has function body (=> expr)");

	// Method 2: translate
	const auto &m1 = imp->methods[1];
	ASSERT(m1->name == "translate", "Method 1 is translate");
	ASSERT(m1->params.size() == 3, "Method 1 has 3 params");
	ASSERT(m1->params[0].name == "self" && m1->params[0].is_mut, "Param 0 is var self");

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

	ASSERT(!p.has_errors(), "Parser must not have errors when parsing logical expressions");
	ASSERT(prog->declarations.size() == 1, "Must parse 1 function");
	auto *fn = as<FnDecl>(prog->declarations[0]);

	// Statement 1: val r1: bool = a && b || c && !d;
	// Precedence: || is top-level binary op
	auto *s0 = as<VarDeclStmt>(fn->body->statements[0]);
	ASSERT(isa<BinaryExpr>(s0->initializer), "Init of r1 must be BinaryExpr");
	auto *top_or = as<BinaryExpr>(s0->initializer);
	ASSERT(top_or->op == TokenType::OR_OR, "Top op of r1 must be ||");
	ASSERT(isa<BinaryExpr>(top_or->left), "Left side of || must be BinaryExpr");
	ASSERT(as<BinaryExpr>(top_or->left)->op == TokenType::AND_AND, "Left side must be &&");
	ASSERT(isa<BinaryExpr>(top_or->right), "Right side of || must be BinaryExpr");
	auto *right_and = as<BinaryExpr>(top_or->right);
	ASSERT(right_and->op == TokenType::AND_AND, "Right side must be &&");
	ASSERT(isa<UnaryExpr>(right_and->right), "Right side of && must be UnaryExpr (!d)");
	ASSERT(as<UnaryExpr>(right_and->right)->op == TokenType::BANG, "Unary op must be !");

	// Statement 2: val r2: bool = (a || b) && c;
	auto *s1 = as<VarDeclStmt>(fn->body->statements[1]);
	ASSERT(isa<BinaryExpr>(s1->initializer), "Init of r2 must be BinaryExpr");
	auto *top_and = as<BinaryExpr>(s1->initializer);
	ASSERT(top_and->op == TokenType::AND_AND, "Top op of r2 must be &&");
	ASSERT(isa<GroupExpr>(top_and->left), "Left side of && must be GroupExpr");

	return true;
}

bool test_parse_module_and_use() {
	std::string_view code =
			"mod math.geometry.point;\n"
			"\n"
			"use math.calc.add;\n"
			"use graphics.Point as GPoint;\n"
			"use std.collections.*;\n"
			"\n"
			"pub fn compute(): i32 {\n"
			"    return 42;\n"
			"}\n"
			"\n"
			"pub struct Vector { pub x: i32, pub y: i32 }\n"
			"pub enum Color { RED, GREEN, BLUE }\n"
			"pub const MAX: i32 = 100;\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser must not have errors when parsing module, use, pub");
	ASSERT(prog->declarations.size() == 8, "Must parse 8 declarations");

	// 1. module math.geometry.point;
	ASSERT(isa<ModuleDecl>(prog->declarations[0]), "decl 0 must be ModuleDecl");
	auto *mod = as<ModuleDecl>(prog->declarations[0]);
	ASSERT(mod->path.size() == 3, "Module path must have 3 parts: math, geometry, point");
	ASSERT(mod->path[0] == "math", "Part 0 is math");
	ASSERT(mod->path[1] == "geometry", "Part 1 is geometry");
	ASSERT(mod->path[2] == "point", "Part 2 is point");

	// 2. use math.calc.add;
	ASSERT(isa<UseDecl>(prog->declarations[1]), "decl 1 must be UseDecl");
	auto *u1 = as<UseDecl>(prog->declarations[1]);
	ASSERT(u1->path.size() == 2 && u1->path[0] == "math" && u1->path[1] == "calc", "u1 path is math.calc");
	ASSERT(u1->symbol_name == "add", "u1 symbol_name is add");
	ASSERT(u1->alias.empty(), "u1 has no alias");
	ASSERT(!u1->is_wildcard, "u1 is not wildcard");

	// 3. use graphics.Point as GPoint;
	ASSERT(isa<UseDecl>(prog->declarations[2]), "decl 2 must be UseDecl");
	auto *u2 = as<UseDecl>(prog->declarations[2]);
	ASSERT(u2->path.size() == 1 && u2->path[0] == "graphics", "u2 path is graphics");
	ASSERT(u2->symbol_name == "Point", "u2 symbol_name is Point");
	ASSERT(u2->alias == "GPoint", "u2 alias is GPoint");
	ASSERT(!u2->is_wildcard, "u2 is not wildcard");

	// 4. use std.collections.*;
	ASSERT(isa<UseDecl>(prog->declarations[3]), "decl 3 must be UseDecl");
	auto *u3 = as<UseDecl>(prog->declarations[3]);
	ASSERT(u3->path.size() == 2 && u3->path[0] == "std" && u3->path[1] == "collections", "u3 path is std.collections");
	ASSERT(u3->is_wildcard, "u3 must be wildcard (*)");

	// 5. pub fn compute()
	ASSERT(isa<FnDecl>(prog->declarations[4]), "decl 4 must be FnDecl");
	auto *fn = as<FnDecl>(prog->declarations[4]);
	ASSERT(fn->name == "compute", "Function name is compute");
	ASSERT(fn->is_pub, "Function compute must have is_pub == true");

	// 6. pub struct Vector
	ASSERT(isa<StructDecl>(prog->declarations[5]), "decl 5 must be StructDecl");
	auto *st = as<StructDecl>(prog->declarations[5]);
	ASSERT(st->name == "Vector", "Struct name is Vector");
	ASSERT(st->is_pub, "Struct Vector must have is_pub == true");

	// 7. pub enum Color
	ASSERT(isa<EnumDecl>(prog->declarations[6]), "decl 6 must be EnumDecl");
	auto *en = as<EnumDecl>(prog->declarations[6]);
	ASSERT(en->name == "Color", "Enum name is Color");
	ASSERT(en->is_pub, "Enum Color must have is_pub == true");

	// 8. pub const MAX
	ASSERT(isa<ConstDecl>(prog->declarations[7]), "decl 7 must be ConstDecl");
	auto *cn = as<ConstDecl>(prog->declarations[7]);
	ASSERT(cn->name == "MAX", "Const name is MAX");
	ASSERT(cn->is_pub, "Const MAX must have is_pub == true");

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

bool test_parse_when_and_if_expr() {
	// 1. when expression with condition and else
	{
		std::string_view code =
				"val x = when (c) {\n"
				"    1 -> 10;\n"
				"    2, 3 -> 20;\n"
				"    else -> 0;\n"
				"};\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto stmt = p.parse_statement();
		ASSERT(isa<VarDeclStmt>(stmt), "stmt must be VarDeclStmt");
		auto v = as<VarDeclStmt>(stmt);
		ASSERT(isa<WhenExpr>(v->initializer), "init must be WhenExpr");
		auto w = as<WhenExpr>(v->initializer);
		ASSERT(w->condition != nullptr, "when condition must not be null");
		ASSERT(w->arms.size() == 3, "when must have 3 arms");
		ASSERT(!w->arms[0].is_else && w->arms[0].patterns.size() == 1, "arm 0 has 1 pattern");
		ASSERT(!w->arms[1].is_else && w->arms[1].patterns.size() == 2, "arm 1 has 2 patterns");
		ASSERT(w->arms[2].is_else, "arm 2 is else");
	}

	// 2. when statement without condition (boolean when)
	{
		std::string_view code =
				"when {\n"
				"    x > 0 -> foo();\n"
				"    else -> bar();\n"
				"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto stmt = p.parse_statement();
		ASSERT(isa<WhenStmt>(stmt), "stmt must be WhenStmt");
		auto w = as<WhenStmt>(stmt);
		ASSERT(w->condition == nullptr, "boolean when has null condition");
		ASSERT(w->arms.size() == 2, "boolean when has 2 arms");
	}

	// 3. if-else expression (unbraced and braced)
	{
		std::string_view code = "val a = if (x > 0) 1 else -1;";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto stmt = p.parse_statement();
		ASSERT(isa<VarDeclStmt>(stmt), "stmt must be VarDeclStmt");
		auto v = as<VarDeclStmt>(stmt);
		ASSERT(isa<IfExpr>(v->initializer), "init must be IfExpr");
	}
	{
		std::string_view code = "val a = if (x > 0) { 1 } else { -1 };";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto stmt = p.parse_statement();
		ASSERT(isa<VarDeclStmt>(stmt), "stmt must be VarDeclStmt");
		auto v = as<VarDeclStmt>(stmt);
		ASSERT(isa<IfExpr>(v->initializer), "init must be IfExpr");
	}

	// 4. if statement without braces
	{
		std::string_view code = "if (x > 0) return 1; else return -1;";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto stmt = p.parse_statement();
		ASSERT(isa<IfStmt>(stmt), "stmt must be IfStmt");
		auto ifs = as<IfStmt>(stmt);
		ASSERT(isa<BlockStmt>(ifs->then_branch), "then branch wrapped in block");
		ASSERT(isa<BlockStmt>(ifs->else_branch), "else branch wrapped in block");
	}

	return true;
}

bool test_parse_generic_structs() {
	// 1. Generic struct declarations
	{
		std::string_view code =
				"struct Box<T> { value: T }\n"
				"struct Pair<T, U> { first: T, second: U }\n"
				"struct Container<T: Comparable> { item: T }\n"
				"impl<T: Comparable> Container<T> {\n"
				"    fn get(val self): T => self.item;\n"
				"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		ASSERT(!p.has_errors(), "Parser should not error on generic structs");
		ASSERT(prog->declarations.size() == 4, "Expected 4 declarations");

		auto *box = as<StructDecl>(prog->declarations[0]);
		ASSERT(box->name == "Box", "Expected struct Box");
		ASSERT(box->type_params.size() == 1, "Expected 1 type param for Box");
		ASSERT(box->type_params[0].name == "T", "Expected type param T");
		ASSERT(box->fields.size() == 1, "Expected 1 field for Box");

		auto *pair = as<StructDecl>(prog->declarations[1]);
		ASSERT(pair->name == "Pair", "Expected struct Pair");
		ASSERT(pair->type_params.size() == 2, "Expected 2 type params for Pair");
		ASSERT(pair->type_params[0].name == "T" && pair->type_params[1].name == "U", "Expected T and U");

		auto *container = as<StructDecl>(prog->declarations[2]);
		ASSERT(container->name == "Container", "Expected struct Container");
		ASSERT(container->type_params.size() == 1, "Expected 1 type param for Container");
		ASSERT(!container->type_params[0].bounds.empty(), "Expected bounds for Container T");
		ASSERT(container->type_params[0].bounds[0] == "Comparable", "Expected Comparable bound");
		ASSERT(container->fields.size() == 1, "Expected 1 field in Container");

		auto *imp = as<ImplDecl>(prog->declarations[3]);
		ASSERT(imp->struct_name == "Container", "Expected impl Container");
		ASSERT(imp->methods.size() == 1, "Expected 1 method in Container impl");
	}

	// 2. Generic type usage and constructor call with explicit and inferred type arguments
	{
		std::string_view code =
				"fn main(): i32 {\n"
				"    val b: Box<i32> = Box<i32>(10);\n"
				"    val p: Pair<i32, str> = Pair(1, \"hello\");\n"
				"    val nested: Box<Box<i32>> = Box<Box<i32>>(b);\n"
				"    return 0;\n"
				"}\n";
		Lexer lex{code};
		Parser p{lex.tokenize()};
		auto prog = p.parse_program();
		ASSERT(!p.has_errors(), "Parser should parse generic type usage and calls");
		ASSERT(prog->declarations.size() == 1, "Expected 1 fn declaration");

		auto *fn = as<FnDecl>(prog->declarations[0]);
		ASSERT(fn->body->statements.size() == 4, "Expected 4 statements in main");

		// val b: Box<i32> = Box<i32>(10);
		auto *v0 = as<VarDeclStmt>(fn->body->statements[0]);
		ASSERT(isa<NamedType>(v0->type_annotation), "Expected NamedType");
		auto *nt0 = as<NamedType>(v0->type_annotation);
		ASSERT(nt0->name == "Box", "Expected Box");
		ASSERT(nt0->type_args.size() == 1, "Expected 1 type argument");
		ASSERT(isa<CallExpr>(v0->initializer), "Expected CallExpr");
		auto *c0 = as<CallExpr>(v0->initializer);
		ASSERT(c0->type_args.size() == 1, "Expected 1 explicit type arg in call");

		// val nested: Box<Box<i32>>
		auto *v2 = as<VarDeclStmt>(fn->body->statements[2]);
		auto *nt2 = as<NamedType>(v2->type_annotation);
		ASSERT(nt2->type_args.size() == 1, "Expected 1 type arg for outer Box");
		ASSERT(isa<NamedType>(nt2->type_args[0]), "Expected inner NamedType");
		auto *inner = as<NamedType>(nt2->type_args[0]);
		ASSERT(inner->name == "Box" && inner->type_args.size() == 1, "Expected inner Box<i32>");
	}

	return true;
}

bool test_parse_generic_functions() {
	std::string_view code =
			"fn id<T>(x: T): T => x;\n"
			"fn max<T: Comparable>(a: T, b: T): T {\n"
			"    if (a > b) return a; else return b;\n"
			"}\n"
			"fn main(): i32 {\n"
			"    val a = id<i32>(42);\n"
			"    val b = id(42);\n"
			"    val m = max<i64>(10L, 20L);\n"
			"    return 0;\n"
			"}\n";
	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto prog = p.parse_program();
	ASSERT(!p.has_errors(), "Parser should not error on generic functions");
	ASSERT(prog->declarations.size() == 3, "Expected 3 declarations");

	auto *id_fn = as<FnDecl>(prog->declarations[0]);
	ASSERT(id_fn->name == "id", "Expected fn id");
	ASSERT(id_fn->type_params.size() == 1, "Expected 1 type param for id");
	ASSERT(id_fn->type_params[0].name == "T", "Expected type param T");
	ASSERT(id_fn->params.size() == 1, "Expected 1 param for id");

	auto *max_fn = as<FnDecl>(prog->declarations[1]);
	ASSERT(max_fn->name == "max", "Expected fn max");
	ASSERT(max_fn->type_params.size() == 1, "Expected 1 type param for max");
	ASSERT(max_fn->type_params[0].name == "T", "Expected type param T");
	ASSERT(!max_fn->type_params[0].bounds.empty(), "Expected bound for max T");
	ASSERT(max_fn->type_params[0].bounds[0] == "Comparable", "Expected Comparable bound");

	auto *main_fn = as<FnDecl>(prog->declarations[2]);
	ASSERT(main_fn->body->statements.size() == 4, "Expected 4 statements in main");
	auto *v0 = as<VarDeclStmt>(main_fn->body->statements[0]);
	auto *c0 = as<CallExpr>(v0->initializer);
	ASSERT(c0->type_args.size() == 1, "Expected 1 explicit type argument in id<i32>(42)");

	auto *v1 = as<VarDeclStmt>(main_fn->body->statements[1]);
	auto *c1 = as<CallExpr>(v1->initializer);
	ASSERT(c1->type_args.empty(), "Expected empty explicit type arguments in id(42)");

	return true;
}

bool test_parse_traits() {
	std::string_view code =
			"trait Greeter {\n"
			"    fn greet(val self): str => \"hello\";\n"
			"    fn name(val self): str;\n"
			"}\n"
			"\n"
			"trait AdvancedGreeter : Greeter {\n"
			"    fn detailed_greet(val self): str;\n"
			"}\n"
			"\n"
			"struct Person {\n"
			"    pub first: str,\n"
			"    pub age: i32\n"
			"}\n"
			"\n"
			"impl Greeter for Person {\n"
			"    fn name(val self): str => self.first;\n"
			"}\n"
			"\n"
			"impl AdvancedGreeter for Person {\n"
			"    pub fn detailed_greet(val self): str => self.first;\n"
			"}\n";

	Lexer lex{code};
	Parser p{lex.tokenize()};
	auto *prog = p.parse_program();

	ASSERT(!p.has_errors(), "Parser should not error on traits");
	ASSERT(prog->declarations.size() == 5, "Expected 5 declarations");

	// 1. Trait Greeter
	ASSERT(isa<TraitDecl>(prog->declarations[0]), "Expected TraitDecl for Greeter");
	auto *tr1 = as<TraitDecl>(prog->declarations[0]);
	ASSERT(tr1->name == "Greeter", "Trait name should be Greeter");
	ASSERT(tr1->bases.empty(), "Greeter has no base traits");
	ASSERT(tr1->methods.size() == 2, "Greeter has 2 methods");
	ASSERT(tr1->methods[0]->name == "greet", "Method 0 is greet");
	ASSERT(tr1->methods[0]->body != nullptr, "greet has default body");
	ASSERT(tr1->methods[1]->name == "name", "Method 1 is name");
	ASSERT(tr1->methods[1]->body == nullptr, "name is prototype without body");

	// 2. Trait AdvancedGreeter : Greeter
	ASSERT(isa<TraitDecl>(prog->declarations[1]), "Expected TraitDecl for AdvancedGreeter");
	auto *tr2 = as<TraitDecl>(prog->declarations[1]);
	ASSERT(tr2->name == "AdvancedGreeter", "Trait name should be AdvancedGreeter");
	ASSERT(tr2->bases.size() == 1 && tr2->bases[0] == "Greeter", "AdvancedGreeter inherits Greeter");
	ASSERT(tr2->methods.size() == 1, "AdvancedGreeter has 1 method");

	// 3. Struct Person
	ASSERT(isa<StructDecl>(prog->declarations[2]), "Expected StructDecl for Person");
	auto *st = as<StructDecl>(prog->declarations[2]);
	ASSERT(st->name == "Person", "Struct name should be Person");
	ASSERT(st->fields.size() == 2, "Person has 2 fields");

	// 4. Impl Greeter for Person
	ASSERT(isa<ImplDecl>(prog->declarations[3]), "Expected ImplDecl for Greeter");
	auto *imp1 = as<ImplDecl>(prog->declarations[3]);
	ASSERT(imp1->trait_name == "Greeter", "Trait name is Greeter");
	ASSERT(imp1->struct_name == "Person", "Struct name is Person");
	ASSERT(imp1->methods.size() == 1, "Greeter impl has 1 method");
	ASSERT(imp1->methods[0]->name == "name", "name method parsed");

	// 5. Impl AdvancedGreeter for Person
	ASSERT(isa<ImplDecl>(prog->declarations[4]), "Expected ImplDecl for AdvancedGreeter");
	auto *imp2 = as<ImplDecl>(prog->declarations[4]);
	ASSERT(imp2->trait_name == "AdvancedGreeter", "Trait name is AdvancedGreeter");
	ASSERT(imp2->struct_name == "Person", "Struct name is Person");
	ASSERT(imp2->methods.size() == 1, "AdvancedGreeter impl has 1 method");
	ASSERT(imp2->methods[0]->name == "detailed_greet" && imp2->methods[0]->is_pub, "detailed_greet has is_pub");

	return true;
}

int main() {
	std::cout << "[RUNNING] Parser tests..." << std::endl;

	if (!test_parse_traits()) return 1;
	std::cout << "  [PASS] test_parse_traits" << std::endl;

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

	if (!test_parse_new_struct_and_impl()) return 1;
	std::cout << "  [PASS] test_parse_new_struct_and_impl" << std::endl;

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

	if (!test_parse_when_and_if_expr()) return 1;
	std::cout << "  [PASS] test_parse_when_and_if_expr" << std::endl;

	if (!test_parse_generic_structs()) return 1;
	std::cout << "  [PASS] test_parse_generic_structs" << std::endl;

	if (!test_parse_generic_functions()) return 1;
	std::cout << "  [PASS] test_parse_generic_functions" << std::endl;

	if (!test_error_recovery()) return 1;
	std::cout << "  [PASS] test_error_recovery" << std::endl;

	std::cout << "[ALL PASSED] Parser tests passed successfully!" << std::endl;
	return 0;
}
