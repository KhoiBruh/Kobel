module;

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

export module ast.stmt;

import token;
import ast.base;
import ast.type;
import ast.expr;

// ============================================================================
// 5. Statements (Stmt)
// ============================================================================

export struct Stmt : ASTNode {
	using ASTNode::ASTNode;
};

export struct BlockStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_BLOCK;
	std::vector<std::unique_ptr<Stmt>> statements;

	explicit BlockStmt(
		std::vector<std::unique_ptr<Stmt>> stmts,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), statements(std::move(stmts)) {}
};

export struct ExprStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_EXPR;
	std::unique_ptr<Expr> expr;

	explicit ExprStmt(
		std::unique_ptr<Expr> e,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), expr(std::move(e)) {}
};

export struct VarDeclStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_VAR_DECL;
	bool is_mut; // true: var, false: val
	std::string_view name;
	std::unique_ptr<TypeNode> type_annotation; // nullptr if type inferred
	std::unique_ptr<Expr> initializer;

	VarDeclStmt(
		const bool mut,
		const std::string_view n,
		std::unique_ptr<TypeNode> ty,
		std::unique_ptr<Expr> init,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
	is_mut(mut), name(n),
	type_annotation(std::move(ty)),
	initializer(std::move(init)) {}
};

export struct IfStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_IF;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<BlockStmt> then_branch;
	std::unique_ptr<Stmt> else_branch; // can be BlockStmt or IfStmt

	IfStmt(
		std::unique_ptr<Expr> cond,
		std::unique_ptr<BlockStmt> th,
		std::unique_ptr<Stmt> el = nullptr,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
	condition(std::move(cond)),
	then_branch(std::move(th)),
	else_branch(std::move(el)) {}
};

export struct WhileStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_WHILE;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<BlockStmt> body;

	WhileStmt(
		std::unique_ptr<Expr> cond,
		std::unique_ptr<BlockStmt> b,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
	condition(std::move(cond)),
	body(std::move(b)) {}
};

export struct ReturnStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_RETURN;
	std::unique_ptr<Expr> value; // nullptr if return void;

	explicit ReturnStmt(
		std::unique_ptr<Expr> val = nullptr,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), value(std::move(val)) {}
};

export struct BreakStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_BREAK;
	explicit BreakStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {}
};

export struct ContinueStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_CONTINUE;
	explicit ContinueStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {}
};
