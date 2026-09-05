#include <iostream>
#include <memory>
#include <string_view>

import token;
import ast;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool test_type_nodes() {
	// NamedType: i32
	auto named = std::make_unique<NamedType>("i32", 1, 5);
	ASSERT(named->name == "i32", "NamedType name mismatch");
	ASSERT(named->kind == ASTKind::TYPE_NAMED, "NamedType kind mismatch");
	ASSERT(isa<NamedType>(named.get()), "isa<NamedType> failed");

	// PointerType: *i32
	auto ptr = std::make_unique<PointerType>(false, std::move(named), 1, 4);
	ASSERT(!ptr->is_mut, "PointerType mutability mismatch");
	ASSERT(ptr->pointee != nullptr, "PointerType pointee is null");
	ASSERT(isa<PointerType>(ptr.get()), "isa<PointerType> failed");

	return true;
}

bool test_expr_nodes() {
	// LiteralExpr
	auto lit = std::make_unique<LiteralExpr>(LiteralKind::INT, "42", 1, 1);
	ASSERT(lit->raw_text == "42", "LiteralExpr text mismatch");
	ASSERT(lit->literal_kind == LiteralKind::INT, "LiteralKind mismatch");
	ASSERT(isa<LiteralExpr>(lit.get()), "isa<LiteralExpr> failed");

	// IdentifierExpr
	auto id = std::make_unique<IdentifierExpr>("foo", 1, 1);
	ASSERT(id->name == "foo", "IdentifierExpr name mismatch");
	ASSERT(isa<IdentifierExpr>(id.get()), "isa<IdentifierExpr> failed");

	// BinaryExpr: a + b
	auto left = std::make_unique<IdentifierExpr>("a", 1, 1);
	auto right = std::make_unique<IdentifierExpr>("b", 1, 5);
	auto bin = std::make_unique<BinaryExpr>(std::move(left), TokenType::PLUS, std::move(right), 1, 3);
	ASSERT(bin->op == TokenType::PLUS, "BinaryExpr op mismatch");
	ASSERT(isa<BinaryExpr>(bin.get()), "isa<BinaryExpr> failed");

	// CallExpr: foo(42)
	std::vector<std::unique_ptr<Expr>> args;
	args.push_back(std::make_unique<LiteralExpr>(LiteralKind::INT, "42", 1, 5));
	auto call = std::make_unique<CallExpr>(std::move(id), std::move(args), 1, 1);
	ASSERT(call->args.size() == 1, "CallExpr args size mismatch");
	ASSERT(isa<CallExpr>(call.get()), "isa<CallExpr> failed");

	// CastExpr: x as u32
	auto x_expr = std::make_unique<IdentifierExpr>("x", 1, 1);
	auto u32_type = std::make_unique<NamedType>("u32", 1, 6);
	auto cast = std::make_unique<CastExpr>(std::move(x_expr), std::move(u32_type), 1, 3);
	ASSERT(cast->expr != nullptr && cast->target_type != nullptr, "CastExpr members null");
	ASSERT(isa<CastExpr>(cast.get()), "isa<CastExpr> failed");

	return true;
}

bool test_stmt_nodes() {
	// VarDeclStmt: val x: i32 = 10;
	auto ty = std::make_unique<NamedType>("i32", 1, 8);
	auto val = std::make_unique<LiteralExpr>(LiteralKind::INT, "10", 1, 14);
	auto var_decl = std::make_unique<VarDeclStmt>(false, "x", std::move(ty), std::move(val), 1, 1);
	ASSERT(!var_decl->is_mut, "VarDeclStmt mutability mismatch");
	ASSERT(var_decl->name == "x", "VarDeclStmt name mismatch");
	ASSERT(isa<VarDeclStmt>(var_decl.get()), "isa<VarDeclStmt> failed");

	// ReturnStmt: return x;
	auto ret = std::make_unique<ReturnStmt>(std::make_unique<IdentifierExpr>("x", 2, 8), 2, 1);
	ASSERT(ret->value != nullptr, "ReturnStmt value is null");
	ASSERT(isa<ReturnStmt>(ret.get()), "isa<ReturnStmt> failed");

	// IfStmt
	auto cond = std::make_unique<LiteralExpr>(LiteralKind::BOOL, "true", 3, 5);
	std::vector<std::unique_ptr<Stmt>> then_stmts;
	then_stmts.push_back(std::make_unique<ReturnStmt>(nullptr, 3, 12));
	auto then_block = std::make_unique<BlockStmt>(std::move(then_stmts), 3, 10);
	auto if_stmt = std::make_unique<IfStmt>(std::move(cond), std::move(then_block), nullptr, 3, 1);
	ASSERT(if_stmt->condition != nullptr, "IfStmt cond is null");
	ASSERT(if_stmt->else_branch == nullptr, "IfStmt else should be null");
	ASSERT(isa<IfStmt>(if_stmt.get()), "isa<IfStmt> failed");

	return true;
}

bool test_decl_nodes() {
	// FnDecl: fn add(a: i32): i32 { return a; }
	auto fn = std::make_unique<FnDecl>("add", 1, 1);
	fn->params.push_back(Param{"a", std::make_unique<NamedType>("i32", 1, 11)});
	fn->return_type = std::make_unique<NamedType>("i32", 1, 18);

	std::vector<std::unique_ptr<Stmt>> body_stmts;
	body_stmts.push_back(std::make_unique<ReturnStmt>(std::make_unique<IdentifierExpr>("a", 1, 30), 1, 23));
	fn->body = std::make_unique<BlockStmt>(std::move(body_stmts), 1, 21);

	ASSERT(fn->params.size() == 1, "FnDecl params size mismatch");
	ASSERT(fn->body != nullptr, "FnDecl body is null");
	ASSERT(isa<FnDecl>(fn.get()), "isa<FnDecl> failed");

	// StructDecl: struct Point(x: i32, y: i32)
	auto st = std::make_unique<StructDecl>("Point", 2, 1);
	st->fields.push_back(StructField{"x", std::make_unique<NamedType>("i32", 2, 16)});
	st->fields.push_back(StructField{"y", std::make_unique<NamedType>("i32", 2, 24)});
	ASSERT(st->name == "Point", "StructDecl name mismatch");
	ASSERT(st->fields.size() == 2, "StructDecl field count mismatch");
	ASSERT(isa<StructDecl>(st.get()), "isa<StructDecl> failed");

	// Program node
	auto prog = std::make_unique<Program>();
	prog->declarations.push_back(std::move(fn));
	prog->declarations.push_back(std::move(st));
	ASSERT(prog->declarations.size() == 2, "Program decl count mismatch");
	ASSERT(isa<Program>(prog.get()), "isa<Program> failed");

	return true;
}

bool test_rtti() {
	std::unique_ptr<ASTNode> node = std::make_unique<FnDecl>("compute", 1, 1);

	ASSERT(isa<FnDecl>(node.get()), "isa<FnDecl> should be true");
	ASSERT(!isa<StructDecl>(node.get()), "isa<StructDecl> should be false");
	ASSERT(!isa<BinaryExpr>(node.get()), "isa<BinaryExpr> should be false");

	FnDecl* fn = as<FnDecl>(node.get());
	ASSERT(fn != nullptr, "as<FnDecl> should return valid pointer");
	ASSERT(fn->name == "compute", "Casted pointer name mismatch");

	StructDecl* st = as<StructDecl>(node.get());
	ASSERT(st == nullptr, "as<StructDecl> should return nullptr");

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

	if (!test_rtti()) return 1;
	std::cout << "  [PASS] test_rtti" << std::endl;

	std::cout << "[ALL PASSED] AST tests passed successfully!" << std::endl;
	return 0;
}
