module;

#include <cstdint>
#include <memory>
#include <optional>
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
	TYPE_NULLABLE,
	TYPE_GENERIC,

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
	EXPR_ARRAY_LITERAL,

	// Statements
	STMT_BLOCK,
	STMT_EXPR,
	STMT_VAR_DECL,
	STMT_IF,
	STMT_WHILE,
	STMT_LOOP,
	STMT_RETURN,
	STMT_DEFER,
	STMT_BREAK,
	STMT_CONTINUE,

	// Declarations
	DECL_FN,
	DECL_STRUCT,
	DECL_ENUM,
	DECL_EXTERN_BLOCK,
	PROGRAM
};

// ============================================================================
// 2. Node Gốc & Cơ chế LLVM-style RTTI (tương thích /GR- / -fno-rtti)
// ============================================================================

export struct ASTNode {
	ASTKind kind;
	size_t line = 0;
	size_t col = 0;

	ASTNode(const ASTKind k, const size_t l = 0, const size_t c = 0)
		: kind(k), line(l), col(c) {}

	virtual ~ASTNode() = default;
};

// Kiểm tra kiểu: isa<T>(node)
export template <typename T>
bool isa(const ASTNode* node) {
	return node && node->kind == T::KIND;
}

// Ép kiểu an toàn không dùng dynamic_cast: as<T>(node)
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

// Kiểu định danh đơn, ví dụ: "i32", "str", "bool", "MyStruct"
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

// Kiểu nullable: T?
export struct NullableType final : TypeNode {
	static constexpr ASTKind KIND = ASTKind::TYPE_NULLABLE;
	std::unique_ptr<TypeNode> inner;

	NullableType(std::unique_ptr<TypeNode> inn, const size_t l = 0, const size_t c = 0)
		: TypeNode(KIND, l, c), inner(std::move(inn)) {}
};

// Kiểu tham số hóa generic: Array<i32>, Map<str, i32>
export struct GenericType final : TypeNode {
	static constexpr ASTKind KIND = ASTKind::TYPE_GENERIC;
	std::string_view base_name;
	std::vector<std::unique_ptr<TypeNode>> type_args;

	GenericType(const std::string_view base, std::vector<std::unique_ptr<TypeNode>> args, const size_t l = 0, const size_t c = 0)
		: TypeNode(KIND, l, c), base_name(base), type_args(std::move(args)) {}
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

// Hằng số nguyên, thực, boolean, char, string, null
export struct LiteralExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_LITERAL;
	LiteralKind literal_kind;
	std::string_view raw_text;

	LiteralExpr(const LiteralKind lk, const std::string_view raw, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), literal_kind(lk), raw_text(raw) {}
};

// Tên biến / định danh (vd: x, my_var)
export struct IdentifierExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_IDENTIFIER;
	std::string_view name;

	IdentifierExpr(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), name(n) {}
};

// Biểu thức nhị phân: a + b, x == y, p && q
export struct BinaryExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_BINARY;
	std::unique_ptr<Expr> left;
	TokenType op;
	std::unique_ptr<Expr> right;

	BinaryExpr(std::unique_ptr<Expr> l, const TokenType o, std::unique_ptr<Expr> r, const size_t ln = 0, const size_t col = 0)
		: Expr(KIND, ln, col), left(std::move(l)), op(o), right(std::move(r)) {}
};

// Biểu thức một ngôi: -x, !cond, *ptr, &val
export struct UnaryExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_UNARY;
	TokenType op;
	std::unique_ptr<Expr> operand;
	bool is_prefix;

	UnaryExpr(const TokenType o, std::unique_ptr<Expr> opnd, const bool prefix = true, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), op(o), operand(std::move(opnd)), is_prefix(prefix) {}
};

// Lệnh gọi hàm hoặc phương thức: foo(1, 2)
export struct CallExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_CALL;
	std::unique_ptr<Expr> callee;
	std::vector<std::unique_ptr<Expr>> args;

	CallExpr(std::unique_ptr<Expr> cl, std::vector<std::unique_ptr<Expr>> a, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), callee(std::move(cl)), args(std::move(a)) {}
};

// Truy cập thành viên: obj.field
export struct MemberExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_MEMBER;
	std::unique_ptr<Expr> object;
	std::string_view member;

	MemberExpr(std::unique_ptr<Expr> obj, const std::string_view mem, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), object(std::move(obj)), member(mem) {}
};

// Chỉ mục mảng/con trỏ: arr[i]
export struct IndexExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_INDEX;
	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> index;

	IndexExpr(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> idx, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), target(std::move(tgt)), index(std::move(idx)) {}
};

// Phép gán: x = 10, a += 2
export struct AssignExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_ASSIGN;
	std::unique_ptr<Expr> target;
	TokenType op;
	std::unique_ptr<Expr> value;

	AssignExpr(std::unique_ptr<Expr> tgt, const TokenType o, std::unique_ptr<Expr> val, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), target(std::move(tgt)), op(o), value(std::move(val)) {}
};

// Ép kiểu: a as u32
export struct CastExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_CAST;
	std::unique_ptr<Expr> expr;
	std::unique_ptr<TypeNode> target_type;

	CastExpr(std::unique_ptr<Expr> e, std::unique_ptr<TypeNode> t, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), expr(std::move(e)), target_type(std::move(t)) {}
};

// Nhóm ngoặc đơn: (a + b)
export struct GroupExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_GROUP;
	std::unique_ptr<Expr> expr;

	GroupExpr(std::unique_ptr<Expr> e, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), expr(std::move(e)) {}
};

// Literal mảng: [1, 2, 3]
export struct ArrayLiteralExpr final : Expr {
	static constexpr ASTKind KIND = ASTKind::EXPR_ARRAY_LITERAL;
	std::vector<std::unique_ptr<Expr>> elements;

	ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elems, const size_t l = 0, const size_t c = 0)
		: Expr(KIND, l, c), elements(std::move(elems)) {}
};

// ============================================================================
// 5. Câu lệnh (Stmt)
// ============================================================================

export struct Stmt : ASTNode {
	using ASTNode::ASTNode;
};

// Khối lệnh: { stmt1; stmt2; }
export struct BlockStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_BLOCK;
	std::vector<std::unique_ptr<Stmt>> statements;

	BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), statements(std::move(stmts)) {}
};

// Biểu thức đóng vai trò câu lệnh: foo();
export struct ExprStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_EXPR;
	std::unique_ptr<Expr> expr;

	ExprStmt(std::unique_ptr<Expr> e, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), expr(std::move(e)) {}
};

// Khai báo biến: val x: i32 = 10; hoặc var y = 20;
export struct VarDeclStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_VAR_DECL;
	bool is_mut; // true: var, false: val
	std::string_view name;
	std::unique_ptr<TypeNode> type_annotation; // có thể rỗng nếu suy luận kiểu
	std::unique_ptr<Expr> initializer;

	VarDeclStmt(const bool mut, const std::string_view n, std::unique_ptr<TypeNode> ty, std::unique_ptr<Expr> init, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), is_mut(mut), name(n), type_annotation(std::move(ty)), initializer(std::move(init)) {}
};

// if (cond) { ... } else { ... }
export struct IfStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_IF;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<Stmt> then_branch;
	std::unique_ptr<Stmt> else_branch; // có thể là nullptr

	IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> th, std::unique_ptr<Stmt> el = nullptr, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), condition(std::move(cond)), then_branch(std::move(th)), else_branch(std::move(el)) {}
};

// while (cond) { ... }
export struct WhileStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_WHILE;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<BlockStmt> body;

	WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<BlockStmt> b, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), condition(std::move(cond)), body(std::move(b)) {}
};

// loop { ... }
export struct LoopStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_LOOP;
	std::unique_ptr<BlockStmt> body;

	LoopStmt(std::unique_ptr<BlockStmt> b, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), body(std::move(b)) {}
};

// return expr; hoặc return;
export struct ReturnStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_RETURN;
	std::unique_ptr<Expr> value; // nullptr nếu return void

	ReturnStmt(std::unique_ptr<Expr> val = nullptr, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), value(std::move(val)) {}
};

// defer cleanup();
export struct DeferStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_DEFER;
	std::unique_ptr<Stmt> deferred;

	DeferStmt(std::unique_ptr<Stmt> d, const size_t l = 0, const size_t c = 0)
		: Stmt(KIND, l, c), deferred(std::move(d)) {}
};

// break;
export struct BreakStmt final : Stmt {
	static constexpr ASTKind KIND = ASTKind::STMT_BREAK;
	BreakStmt(const size_t l = 0, const size_t c = 0) : Stmt(KIND, l, c) {}
};

// continue;
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

// Chế độ truyền tham số hàm (theo Mục 6 đặc tả Kobel)
export enum class ParamMode {
	NORMAL,     // param: T
	MOVE_SELF,  // self: T
	VAL_SELF,   // val self: T
	VAR_SELF    // var self: T
};

export struct Param {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
	ParamMode mode = ParamMode::NORMAL;
};

// Khai báo hàm: fn name(...)[: RetType] { ... } hoặc => expr;
export struct FnDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_FN;
	bool is_pub = false;
	bool is_extern = false;
	std::string_view name;
	std::vector<Param> params;
	std::unique_ptr<TypeNode> return_type; // nullptr nếu void
	std::unique_ptr<BlockStmt> body;       // thân hàm dạng khối ngoặc
	std::unique_ptr<Expr> single_expr_body; // thân hàm dạng biểu thức đơn: => expr

	FnDecl(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n) {}
};

// Field của Primary Constructor trong Struct
export struct StructField {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
};

// Khai báo struct: struct Name(a: i32, b: str) { fn ... }
export struct StructDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_STRUCT;
	bool is_pub = false;
	std::string_view name;
	std::vector<StructField> fields;
	std::vector<std::unique_ptr<FnDecl>> methods;

	StructDecl(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n) {}
};

// Khai báo variant trong enum: A, B = 504
export struct EnumVariant {
	std::string_view name;
	std::optional<int64_t> explicit_value;
};

// Khai báo enum: enum Status : u16 { A, B }
export struct EnumDecl final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_ENUM;
	bool is_pub = false;
	std::string_view name;
	std::unique_ptr<TypeNode> underlying_type; // e.g. u16 (nullptr nếu để compiler tự chọn)
	std::vector<EnumVariant> variants;

	EnumDecl(const std::string_view n, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), name(n) {}
};

// Khai báo FFI: extern "libc" { fn printf(...); }
export struct ExternBlock final : Decl {
	static constexpr ASTKind KIND = ASTKind::DECL_EXTERN_BLOCK;
	std::string_view abi; // "libc", "C"
	std::vector<std::unique_ptr<FnDecl>> declarations;

	ExternBlock(const std::string_view a, const size_t l = 0, const size_t c = 0)
		: Decl(KIND, l, c), abi(a) {}
};

// Node gốc đại diện cho cả tệp nguồn (Program)
export struct Program final : ASTNode {
	static constexpr ASTKind KIND = ASTKind::PROGRAM;
	std::vector<std::unique_ptr<Decl>> declarations;

	Program() : ASTNode(KIND, 1, 1) {}
};
