#include <iostream>
#include <memory>
#include <string_view>
#include <string>

import token;
import ast;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool test_type_nodes() { Arena arena;
	// NamedType: i32
	auto named = arena.alloc<NamedType>("i32", 1, 5);
	ASSERT(named->name == "i32", "NamedType name mismatch");
	ASSERT(named->kind == ASTKind::TYPE_NAMED, "NamedType kind mismatch");
	ASSERT(isa<NamedType>(named), "isa<NamedType> failed");

	// PointerType: *i32
	auto ptr = arena.alloc<PointerType>(false, named, 1, 4);
	ASSERT(!ptr->is_mut, "PointerType mutability mismatch");
	ASSERT(ptr->pointee != nullptr, "PointerType pointee is null");
	ASSERT(isa<PointerType>(ptr), "isa<PointerType> failed");

	return true;
}

bool test_expr_nodes() { Arena arena;
	// LiteralExpr
	auto lit = arena.alloc<LiteralExpr>(LiteralKind::INT, "42", 1, 1);
	ASSERT(lit->raw_text == "42", "LiteralExpr text mismatch");
	ASSERT(lit->literal_kind == LiteralKind::INT, "LiteralKind mismatch");
	ASSERT(isa<LiteralExpr>(lit), "isa<LiteralExpr> failed");

	// IdentifierExpr
	auto id = arena.alloc<IdentifierExpr>("foo", 1, 1);
	ASSERT(id->name == "foo", "IdentifierExpr name mismatch");
	ASSERT(isa<IdentifierExpr>(id), "isa<IdentifierExpr> failed");

	// BinaryExpr: a + b
	auto left = arena.alloc<IdentifierExpr>("a", 1, 1);
	auto right = arena.alloc<IdentifierExpr>("b", 1, 5);
	auto bin = arena.alloc<BinaryExpr>(left, TokenType::PLUS, right, 1, 3);
	ASSERT(bin->op == TokenType::PLUS, "BinaryExpr op mismatch");
	ASSERT(isa<BinaryExpr>(bin), "isa<BinaryExpr> failed");

	// CallExpr: foo(42)
	std::vector<Expr*> args;
	args.push_back(arena.alloc<LiteralExpr>(LiteralKind::INT, "42", 1, 5));
	auto call = arena.alloc<CallExpr>(id, arena.alloc_span<Expr*>(args), 1, 1);
	ASSERT(call->args.size() == 1, "CallExpr args size mismatch");
	ASSERT(isa<CallExpr>(call), "isa<CallExpr> failed");

	// CastExpr: x as u32
	auto x_expr = arena.alloc<IdentifierExpr>("x", 1, 1);
	auto u32_type = arena.alloc<NamedType>("u32", 1, 6);
	auto cast = arena.alloc<CastExpr>(x_expr, u32_type, 1, 3);
	ASSERT(cast->expr != nullptr && cast->target_type != nullptr, "CastExpr members null");
	ASSERT(isa<CastExpr>(cast), "isa<CastExpr> failed");

	return true;
}

bool test_stmt_nodes() { Arena arena;
	// VarDeclStmt: val x: i32 = 10;
	auto ty = arena.alloc<NamedType>("i32", 1, 8);
	auto val = arena.alloc<LiteralExpr>(LiteralKind::INT, "10", 1, 14);
	auto var_decl = arena.alloc<VarDeclStmt>(false, "x", ty, val, 1, 1);
	ASSERT(!var_decl->is_mut, "VarDeclStmt mutability mismatch");
	ASSERT(var_decl->name == "x", "VarDeclStmt name mismatch");
	ASSERT(isa<VarDeclStmt>(var_decl), "isa<VarDeclStmt> failed");

	// ReturnStmt: return x;
	auto ret = arena.alloc<ReturnStmt>(arena.alloc<IdentifierExpr>("x", 2, 8), 2, 1);
	ASSERT(ret->value != nullptr, "ReturnStmt value is null");
	ASSERT(isa<ReturnStmt>(ret), "isa<ReturnStmt> failed");

	// IfStmt
	auto cond = arena.alloc<LiteralExpr>(LiteralKind::BOOL, "true", 3, 5);
	std::vector<Stmt*> then_stmts;
	then_stmts.push_back(arena.alloc<ReturnStmt>(nullptr, 3, 12));
	auto then_block = arena.alloc<BlockStmt>(arena.alloc_span<Stmt*>(then_stmts), 3, 10);
	auto if_stmt = arena.alloc<IfStmt>(cond, then_block, nullptr, 3, 1);
	ASSERT(if_stmt->condition != nullptr, "IfStmt cond is null");
	ASSERT(if_stmt->else_branch == nullptr, "IfStmt else should be null");
	ASSERT(isa<IfStmt>(if_stmt), "isa<IfStmt> failed");

	return true;
}

bool test_decl_nodes() { Arena arena;
	// FnDecl: fn add(a: i32): i32 { return a; }
	auto fn = arena.alloc<FnDecl>("add", 1, 1);
		std::vector<Param> fn_params;
	fn_params.push_back(Param{"a", arena.alloc<NamedType>("i32", 1, 11)});
	fn->params = arena.alloc_span<Param>(fn_params);
	fn->return_type = arena.alloc<NamedType>("i32", 1, 18);

	std::vector<Stmt*> body_stmts;
	body_stmts.push_back(arena.alloc<ReturnStmt>(arena.alloc<IdentifierExpr>("a", 1, 30), 1, 23));
	fn->body = arena.alloc<BlockStmt>(arena.alloc_span<Stmt*>(body_stmts), 1, 21);

	ASSERT(fn->params.size() == 1, "FnDecl params size mismatch");
	ASSERT(fn->body != nullptr, "FnDecl body is null");
	ASSERT(isa<FnDecl>(fn), "isa<FnDecl> failed");

	// StructDecl: struct Point(x: i32, y: i32)
	auto st = arena.alloc<StructDecl>("Point", 2, 1);
		std::vector<StructField> st_fields;
	st_fields.push_back(StructField{"x", arena.alloc<NamedType>("i32", 2, 16)});
	st_fields.push_back(StructField{"y", arena.alloc<NamedType>("i32", 2, 24)});
	st->fields = arena.alloc_span<StructField>(st_fields);
	ASSERT(st->name == "Point", "StructDecl name mismatch");
	ASSERT(st->fields.size() == 2, "StructDecl field count mismatch");
	ASSERT(isa<StructDecl>(st), "isa<StructDecl> failed");

	// ImplDecl: impl Point { ... }
	auto imp = arena.alloc<ImplDecl>("Point", 3, 1);
	std::vector<FnDecl *> imp_methods;
	imp_methods.push_back(fn);
	imp->methods = arena.alloc_span<FnDecl *>(imp_methods);
	ASSERT(imp->struct_name == "Point", "ImplDecl struct_name mismatch");
	ASSERT(imp->methods.size() == 1, "ImplDecl methods count mismatch");
	ASSERT(isa<ImplDecl>(imp), "isa<ImplDecl> failed");

	// Program node
	auto prog = arena.alloc<Program>();
		std::vector<Decl*> prog_decls;
	prog_decls.push_back(fn);
	prog_decls.push_back(st);
	prog_decls.push_back(imp);
	prog->declarations = arena.alloc_span<Decl*>(prog_decls);
	ASSERT(prog->declarations.size() == 3, "Program decl count mismatch");
	ASSERT(isa<Program>(prog), "isa<Program> failed");

	return true;
}


bool test_alloc_string() {
	Arena arena;

	// Empty string
	std::string_view empty_sv = arena.alloc_string("");
	ASSERT(empty_sv.empty(), "Empty string allocation should return empty string_view");
	ASSERT(empty_sv.data() == nullptr, "Empty string allocation should return null data pointer");

	// Large string
	std::string large_str(100000, 'a');
	std::string_view large_sv = arena.alloc_string(large_str);
	ASSERT(large_sv.size() == 100000, "Large string allocation size mismatch");
	ASSERT(large_sv == large_str, "Large string allocation content mismatch");

	return true;
}

bool test_rtti() { Arena arena;
	ASTNode* node = arena.alloc<FnDecl>("compute", 1, 1);

	ASSERT(isa<FnDecl>(node), "isa<FnDecl> should be true");
	ASSERT(!isa<StructDecl>(node), "isa<StructDecl> should be false");
	ASSERT(!isa<ImplDecl>(node), "isa<ImplDecl> should be false");
	ASSERT(!isa<BinaryExpr>(node), "isa<BinaryExpr> should be false");

	FnDecl* fn = as<FnDecl>(node);
	ASSERT(fn != nullptr, "as<FnDecl> should return valid pointer");
	ASSERT(fn->name == "compute", "Casted pointer name mismatch");

	StructDecl* st = as<StructDecl>(node);
	ASSERT(st == nullptr, "as<StructDecl> should return nullptr");

	ImplDecl* imp = as<ImplDecl>(node);
	ASSERT(imp == nullptr, "as<ImplDecl> should return nullptr");

	return true;
}

int main() {
	std::cout << "[RUNNING] AST tests..." << std::endl;
	if (!test_type_nodes()) return 1;
	std::cout << "  [PASS] test_type_nodes" << std::endl;

	if (!test_expr_nodes()) return 1;
	std::cout << "  [PASS] test_expr_nodes" << std::endl;

	if (!test_stmt_nodes()) return 1;
	std::cout << "  [PASS] test_stmt_nodes" << std::endl;

	if (!test_decl_nodes()) return 1;
	std::cout << "  [PASS] test_decl_nodes" << std::endl;

	if (!test_alloc_string()) return 1;
	std::cout << "  [PASS] test_alloc_string" << std::endl;

	if (!test_rtti()) return 1;
	std::cout << "  [PASS] test_rtti" << std::endl;

	std::cout << "[ALL PASSED] AST tests passed successfully!" << std::endl;
	return 0;
}





