module;

#include <span>
#include <string_view>
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
	std::span<Stmt *> statements;

	explicit BlockStmt(
		std::span<Stmt *> stmts,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), statements(stmts) {
	}
};

export struct ExprStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_EXPR;
	Expr *expr;

	explicit ExprStmt(
		Expr *e,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), expr(e) {
	}
};

export struct VarDeclStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_VAR_DECL;
	bool is_mut; // true: var, false: val
	std::string_view name;
	TypeNode *type_annotation; // nullptr if type inferred
	Expr *initializer;

	VarDeclStmt(
		const bool mut,
		const std::string_view n,
		TypeNode *ty,
		Expr *init,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
		is_mut(mut), name(n),
		type_annotation(ty),
		initializer(init) {
	}
};

export struct IfStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_IF;
	Expr *condition;
	BlockStmt *then_branch;
	Stmt *else_branch; // can be BlockStmt or IfStmt

	IfStmt(
		Expr *cond,
		BlockStmt *th,
		Stmt *el = nullptr,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
		condition(cond),
		then_branch(th),
		else_branch(el) {
	}
};

export struct WhileStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_WHILE;
	Expr *condition;
	BlockStmt *body;

	WhileStmt(
		Expr *cond,
		BlockStmt *b,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c),
		condition(cond),
		body(b) {
	}
};

export struct ReturnStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_RETURN;
	Expr *value; // nullptr if return void;

	explicit ReturnStmt(
		Expr *val = nullptr,
		const size_t l = 0, const size_t c = 0
	) : Stmt(KIND, l, c), value(val) {
	}
};

export struct BreakStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_BREAK;

	explicit BreakStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {
	}
};

export struct ContinueStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_CONTINUE;

	explicit ContinueStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {
	}
};

export struct WhenStmtArm {
	std::span<Expr *> patterns; // empty if is_else
	bool is_else = false;
	Stmt *body = nullptr;
	size_t line = 0;
	size_t col = 0;
};

// When statement: when (cond) { pat1 -> stmt1; else -> stmt2; }
export struct WhenStmt final : Stmt {
	static constexpr auto KIND = ASTKind::STMT_WHEN;
	Expr *condition; // nullptr if when { ... }
	std::span<WhenStmtArm> arms;

	WhenStmt(
		Expr *cond,
		std::span<WhenStmtArm> a,
		const size_t l = 0,
		const size_t c = 0
	) : Stmt(KIND, l, c), condition(cond), arms(a) {
	}
};
