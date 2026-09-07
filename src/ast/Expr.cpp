module;

#include <span>
#include <string_view>
#include <utility>
#include <vector>

export module ast.expr;

import token;
import ast.base;
import ast.type;

// ============================================================================
// 4. Expressions (Expr)
// ============================================================================

export struct Expr : ASTNode {
	using ASTNode::ASTNode;
};

export enum class LiteralKind {
	INT,
	FLOAT,
	BOOL,
	CHAR,
	STRING,
	NULL_VAL
};

export struct LiteralExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_LITERAL;
	LiteralKind literal_kind;
	std::string_view raw_text;

	LiteralExpr(
		const LiteralKind lk,
		const std::string_view raw,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), literal_kind(lk), raw_text(raw) {
	}
};

export struct IdentifierExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_IDENTIFIER;
	std::string_view name;

	explicit IdentifierExpr(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), name(n) {
	}
};

export struct BinaryExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_BINARY;
	Expr *left;
	TokenType op;
	Expr *right;

	BinaryExpr(
		Expr *l,
		const TokenType o, Expr *r,
		const size_t ln = 0,
		const size_t col = 0
	) : Expr(KIND, ln, col), left(l), op(o), right(r) {
	}
};

export struct UnaryExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_UNARY;
	TokenType op;
	Expr *operand;

	UnaryExpr(
		const TokenType o,
		Expr *opnd,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), op(o), operand(opnd) {
	}
};

export struct CallExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_CALL;
	Expr *callee;
	std::span<Expr *> args;

	CallExpr(
		Expr *cl,
		std::span<Expr *> a,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), callee(cl), args(a) {
	}
};

export struct MemberExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_MEMBER;
	Expr *object;
	std::string_view member;

	MemberExpr(
		Expr *obj,
		const std::string_view mem,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), object(obj), member(mem) {
	}
};

export struct IndexExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_INDEX;
	Expr *target;
	Expr *index;

	IndexExpr(
		Expr *tgt,
		Expr *idx,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), target(tgt), index(idx) {
	}
};

export struct AssignExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_ASSIGN;
	Expr *target;
	Expr *value;

	AssignExpr(
		Expr *tgt,
		Expr *val,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), target(tgt), value(val) {
	}
};

export struct CastExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_CAST;
	Expr *expr;
	TypeNode *target_type;

	CastExpr(
		Expr *e,
		TypeNode *t,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), expr(e), target_type(t) {
	}
};

export struct GroupExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_GROUP;
	Expr *expr;

	explicit GroupExpr(
		Expr *e,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), expr(e) {
	}
};

// Array literal expression: [expr, expr, ...]
export struct ArrayLiteralExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_ARRAY_LITERAL;
	std::span<Expr *> elements;

	explicit ArrayLiteralExpr(
		std::span<Expr *> elems,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), elements(elems) {
	}
};

// If expression: if (cond) then_expr else else_expr
export struct IfExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_IF;
	Expr *condition;
	Expr *then_branch;
	Expr *else_branch;

	IfExpr(
		Expr *cond,
		Expr *th,
		Expr *el,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), condition(cond), then_branch(th), else_branch(el) {
	}
};

export struct WhenArm {
	std::span<Expr *> patterns; // empty if is_else
	bool is_else = false;
	Expr *body = nullptr;
	size_t line = 0;
	size_t col = 0;
};

// When expression: when (cond) { pat1 -> expr1; else -> expr2; }
export struct WhenExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_WHEN;
	Expr *condition; // nullptr if when { ... }
	std::span<WhenArm> arms;

	WhenExpr(
		Expr *cond,
		std::span<WhenArm> a,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), condition(cond), arms(a) {
	}
};
