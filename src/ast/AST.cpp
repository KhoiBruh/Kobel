module;

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

export module ast;

import token;

// ============================================================================
// 1. Phân loại AST Node (ASTKind)
// ============================================================================

export enum class ASTKind {
	// Types
	TYPE_NAMED,
	TYPE_POINTER,

	// Expressions
	EXPR_LITERAL,
	EXPR_IDENTIFIER,
	EXPR_BINARY,
	EXPR_UNARY,
	EXPR_CALL,
	EXPR_MEMBER,
	EXPR_INDEX,
	EXPR_ASSIGN,
	EXPR_CAST,
	EXPR_GROUP,

	// Statements
	STMT_BLOCK,
	STMT_EXPR,
	STMT_VAR_DECL,
	STMT_IF,
	STMT_WHILE,
	STMT_RETURN,
	STMT_BREAK,
	STMT_CONTINUE,

	// Declarations
	DECL_FN,
	DECL_STRUCT,
	DECL_CONST,
	DECL_EXTERN_BLOCK,
	PROGRAM
};

// ============================================================================
// 2. Node Gốc & LLVM-style RTTI (tương thích /GR- / -fno-rtti)
// ============================================================================

export struct ASTNode {
	ASTKind kind;
	size_t line = 0;
	size_t col = 0;

	ASTNode(const ASTKind k, const size_t l = 0, const size_t c = 0)
		: kind(k), line(l), col(c) {}

	virtual ~ASTNode() = default;
};

export template <typename T>
bool isa(const ASTNode* node) {
	return node && node->kind == T::KIND;
}

export template <typename T>
T* as(ASTNode* node) {
	return isa<T>(node) ? static_cast<T*>(node) : nullptr;
}

export template <typename T>
const T* as(const ASTNode* node) {
	return isa<T>(node) ? static_cast<const T*>(node) : nullptr;
}

// ============================================================================
// 3. Hệ thống Kiểu Cú pháp (TypeNode)
// ============================================================================

export struct TypeNode : ASTNode {
	using ASTNode::ASTNode;
};

// Kiểu định danh đơn: "i32", "u8", "usz", "bool", "char", "MyStruct"
export struct NamedType final : TypeNode {
	static constexpr ASTKind KIND = ASTKind::TYPE_NAMED;
	std::string_view name;

	NamedType(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: TypeNode(KIND, l, c), name(n) {}
};

// Kiểu con trỏ: *T (chỉ đọc) hoặc &T (đọc/ghi)
export struct PointerType final : TypeNode {
	static constexpr ASTKind KIND = ASTKind::TYPE_POINTER;
	bool is_mut; // true: &T, false: *T
	std::unique_ptr<TypeNode> pointee;

	PointerType(const bool mut, std::unique_ptr<TypeNode> p, const size_t l = 0, const size_t c = 0)
		: TypeNode(KIND, l, c), is_mut(mut), pointee(std::move(p)) {}
};

// ============================================================================
// 4. Biểu thức (Expr)
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
	static constexpr ASTKind KIND = ASTKind::EXPR_LITERAL;
	LiteralKind literal_kind;
	std::string_view raw_text;

	LiteralExpr(const LiteralKind lk, const std::string_view raw, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), literal_kind(lk), raw_text(raw) {}
};

export struct IdentifierExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_IDENTIFIER;
	std::string_view name;

	IdentifierExpr(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), name(n) {}
};

export struct BinaryExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_BINARY;
	std::unique_ptr<Expr> left;
	TokenType op;
	std::unique_ptr<Expr> right;

	BinaryExpr(std::unique_ptr<Expr> l, const TokenType o, std::unique_ptr<Expr> r, const size_t ln = 0, const size_t col = 0)
		: Expr(KIND, ln, col), left(std::move(l)), op(o), right(std::move(r)) {}
};

export struct UnaryExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_UNARY;
	TokenType op;
	std::unique_ptr<Expr> operand;

	UnaryExpr(const TokenType o, std::unique_ptr<Expr> opnd, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), op(o), operand(std::move(opnd)) {}
};

export struct CallExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_CALL;
	std::unique_ptr<Expr> callee;
	std::vector<std::unique_ptr<Expr>> args;

	CallExpr(std::unique_ptr<Expr> cl, std::vector<std::unique_ptr<Expr>> a, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), callee(std::move(cl)), args(std::move(a)) {}
};

export struct MemberExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_MEMBER;
	std::unique_ptr<Expr> object;
	std::string_view member;

	MemberExpr(std::unique_ptr<Expr> obj, const std::string_view mem, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), object(std::move(obj)), member(mem) {}
};

export struct IndexExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_INDEX;
	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> index;

	IndexExpr(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> idx, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), target(std::move(tgt)), index(std::move(idx)) {}
};

export struct AssignExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_ASSIGN;
	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> value;

	AssignExpr(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> val, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), target(std::move(tgt)), value(std::move(val)) {}
};

export struct CastExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_CAST;
	std::unique_ptr<Expr> expr;
	std::unique_ptr<TypeNode> target_type;

	CastExpr(std::unique_ptr<Expr> e, std::unique_ptr<TypeNode> t, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), expr(std::move(e)), target_type(std::move(t)) {}
};

export struct GroupExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_GROUP;
	std::unique_ptr<Expr> expr;

	GroupExpr(std::unique_ptr<Expr> e, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), expr(std::move(e)) {}
};

// ============================================================================
// 5. Câu lệnh (Stmt)
// ============================================================================

export struct Stmt : ASTNode {
	using ASTNode::ASTNode;
};

export struct BlockStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_BLOCK;
	std::vector<std::unique_ptr<Stmt>> statements;

	BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), statements(std::move(stmts)) {}
};

export struct ExprStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_EXPR;
	std::unique_ptr<Expr> expr;

	ExprStmt(std::unique_ptr<Expr> e, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), expr(std::move(e)) {}
};

export struct VarDeclStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_VAR_DECL;
	bool is_mut; // true: var, false: val
	std::string_view name;
	std::unique_ptr<TypeNode> type_annotation; // nullptr nếu suy luận kiểu
	std::unique_ptr<Expr> initializer;

	VarDeclStmt(const bool mut, const std::string_view n, std::unique_ptr<TypeNode> ty, std::unique_ptr<Expr> init, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), is_mut(mut), name(n), type_annotation(std::move(ty)), initializer(std::move(init)) {}
};

export struct IfStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_IF;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<BlockStmt> then_branch;
	std::unique_ptr<Stmt> else_branch; // có thể là BlockStmt hoặc IfStmt

	IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<BlockStmt> th, std::unique_ptr<Stmt> el = nullptr, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), condition(std::move(cond)), then_branch(std::move(th)), else_branch(std::move(el)) {}
};

export struct WhileStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_WHILE;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<BlockStmt> body;

	WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<BlockStmt> b, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), condition(std::move(cond)), body(std::move(b)) {}
};

export struct ReturnStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_RETURN;
	std::unique_ptr<Expr> value; // nullptr nếu return void;

	ReturnStmt(std::unique_ptr<Expr> val = nullptr, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), value(std::move(val)) {}
};

export struct BreakStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_BREAK;
	BreakStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {}
};

export struct ContinueStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_CONTINUE;
	ContinueStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {}
};

// ============================================================================
// 6. Khai báo Cấp cao (Decl)
// ============================================================================

export struct Decl : ASTNode {
	using ASTNode::ASTNode;
};

export struct Param {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
};

// Hàm thuần túy: fn name(a: i32, b: i32): i32 { ... }
export struct FnDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_FN;
	std::string_view name;
	std::vector<Param> params;
	std::unique_ptr<TypeNode> return_type; // nullptr nếu void
	std::unique_ptr<BlockStmt> body;       // nullptr nếu prototype (trong extern)

	FnDecl(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n) {}
};

export struct StructField {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
};

// Struct dữ liệu thuần C: struct Point(x: i32, y: i32)
export struct StructDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_STRUCT;
	std::string_view name;
	std::vector<StructField> fields;

	StructDecl(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n) {}
};

// Hằng số top-level: const MAX_SIZE: i32 = 100;
export struct ConstDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_CONST;
	std::string_view name;
	std::unique_ptr<TypeNode> type;
	std::unique_ptr<Expr> value;

	ConstDecl(const std::string_view n, std::unique_ptr<TypeNode> ty, std::unique_ptr<Expr> val, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n), type(std::move(ty)), value(std::move(val)) {}
};

// Khai báo FFI: extern "libc" { fn printf(fmt: *char): i32; }
export struct ExternBlock final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_EXTERN_BLOCK;
	std::string_view abi; // "libc", "C"
	std::vector<std::unique_ptr<FnDecl>> declarations;

	ExternBlock(const std::string_view a, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), abi(a) {}
};

export struct Program final : ASTNode {
	static constexpr ASTKind KIND = ASTKind::PROGRAM;
	std::vector<std::unique_ptr<Decl>> declarations;

	Program() : ASTNode(KIND, 1, 1) {}
};
