module;

#include <memory>
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
	) : Expr(KIND, l, c), literal_kind(lk), raw_text(raw) {}
};

export struct IdentifierExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_IDENTIFIER;
	std::string_view name;

	explicit IdentifierExpr(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), name(n) {}
};

export struct BinaryExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_BINARY;
	std::unique_ptr<Expr> left;
	TokenType op;
	std::unique_ptr<Expr> right;

	BinaryExpr(
		std::unique_ptr<Expr> l,
		const TokenType o, std::unique_ptr<Expr> r,
		const size_t ln = 0,
		const size_t col = 0
	) : Expr(KIND, ln, col), left(std::move(l)), op(o), right(std::move(r)) {}
};

export struct UnaryExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_UNARY;
	TokenType op;
	std::unique_ptr<Expr> operand;

	UnaryExpr(
		const TokenType o,
		std::unique_ptr<Expr> opnd,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), op(o), operand(std::move(opnd)) {}
};

export struct CallExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_CALL;
	std::unique_ptr<Expr> callee;
	std::vector<std::unique_ptr<Expr>> args;

	CallExpr(
		std::unique_ptr<Expr> cl,
		std::vector<std::unique_ptr<Expr>> a,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), callee(std::move(cl)), args(std::move(a)) {}
};

export struct MemberExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_MEMBER;
	std::unique_ptr<Expr> object;
	std::string_view member;

	MemberExpr(
		std::unique_ptr<Expr> obj,
		const std::string_view mem,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), object(std::move(obj)), member(mem) {}
};

export struct IndexExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_INDEX;
	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> index;

	IndexExpr(
		std::unique_ptr<Expr> tgt,
		std::unique_ptr<Expr> idx,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), target(std::move(tgt)), index(std::move(idx)) {}
};

export struct AssignExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_ASSIGN;
	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> value;

	AssignExpr(
		std::unique_ptr<Expr> tgt,
		std::unique_ptr<Expr> val,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), target(std::move(tgt)), value(std::move(val)) {}
};

export struct CastExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_CAST;
	std::unique_ptr<Expr> expr;
	std::unique_ptr<TypeNode> target_type;

	CastExpr(
		std::unique_ptr<Expr> e,
		std::unique_ptr<TypeNode> t,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), expr(std::move(e)), target_type(std::move(t)) {}
};

export struct GroupExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_GROUP;
	std::unique_ptr<Expr> expr;

	explicit GroupExpr(
		std::unique_ptr<Expr> e,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), expr(std::move(e)) {}
};

// Array literal expression: [expr, expr, ...]
export struct ArrayLiteralExpr final : Expr {
	static constexpr auto KIND = ASTKind::EXPR_ARRAY_LITERAL;
	std::vector<std::unique_ptr<Expr>> elements;

	explicit ArrayLiteralExpr(
		std::vector<std::unique_ptr<Expr>> elems,
		const size_t l = 0,
		const size_t c = 0
	) : Expr(KIND, l, c), elements(std::move(elems)) {}
};

