module;

#include <cstdint>

export module ast.base;

// ============================================================================
// 1. Phân loại AST Node (ASTKind)
// ============================================================================

export enum class ASTKind {
	// Types
	TYPE_NAMED,
	TYPE_POINTER,
	TYPE_ARRAY,

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
	STMT_RETURN,
	STMT_BREAK,
	STMT_CONTINUE,

	// Declarations
	DECL_FN,
	DECL_STRUCT,
	DECL_ENUM,
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

	explicit ASTNode(const ASTKind k, const size_t l = 0, const size_t c = 0)
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
