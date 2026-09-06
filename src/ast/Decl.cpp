module;

#include <span>
#include <string>
#include <string_view>

export module ast.decl;

import token;
import ast.base;
import ast.type;
import ast.expr;
import ast.stmt;

// ============================================================================
// 6. Declarations (Decl)
// ============================================================================

export struct Decl : ASTNode {
	using ASTNode::ASTNode;
	bool is_pub = false;
};

// Module declaration: module a.b.c;
export struct ModuleDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_MODULE;
	std::span<std::string_view> path;
	std::string_view full_path;

	explicit ModuleDecl(
		std::span<std::string_view> p,
		std::string_view fp,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), path(p), full_path(fp) {}
};

// Use declaration: use a.b.c.A; or use a.b.c.A as B; or use a.b.c.*;
export struct UseDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_USE;
	std::span<std::string_view> path;
	std::string_view full_path;
	std::string_view symbol_name;
	std::string_view alias;
	bool is_wildcard = false;

	UseDecl(
		std::span<std::string_view> p,
		std::string_view fp,
		const std::string_view sym,
		const std::string_view al,
		const bool wildcard,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), path(p), full_path(fp), symbol_name(sym), alias(al), is_wildcard(wildcard) {}
};

export struct Param {
	std::string_view name;
	TypeNode* type;
	bool is_mut = false; // true if var self / var param
	bool has_val = false; // true if val self / val param
};

// Function declaration: fn name(a: i32, b: i32): i32 { ... }
export struct FnDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_FN;
	std::string_view name;
	std::span<Param> params;
	TypeNode* return_type = nullptr; // nullptr if void
	BlockStmt* body = nullptr;       // nullptr if prototype (in extern)

	explicit FnDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

export struct StructField {
	std::string_view name;
	TypeNode* type;
};

// Struct declaration: struct Point(x: i32, y: i32) { ... }
export struct StructDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_STRUCT;
	std::string_view name;
	std::span<StructField> fields;
	std::span<FnDecl*> methods;

	explicit StructDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

export struct EnumMember {
	std::string_view name;
	Expr* value; // nullptr if auto-incremented
	size_t line = 0;
	size_t col = 0;
};

// Enum declaration: enum Status : u16 { A, B = 504, C }
export struct EnumDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_ENUM;
	std::string_view name;
	TypeNode* underlying_type = nullptr; // nullptr if default i32
	std::span<EnumMember> members;

	explicit EnumDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

// Top-level constant: const MAX_SIZE: i32 = 100;
export struct ConstDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_CONST;
	std::string_view name;
	TypeNode* type;
	Expr* value;

	ConstDecl(
		const std::string_view n,
		TypeNode* ty,
		Expr* val,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n), type(ty), value(val) {}
};

// FFI declaration: extern "libc" { fn printf(fmt: *char): i32; }
export struct ExternBlock final : Decl {
	static constexpr auto KIND = ASTKind::DECL_EXTERN_BLOCK;
	std::string_view abi; // "libc", "C"
	std::span<FnDecl*> declarations;

	explicit ExternBlock(
		const std::string_view a,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), abi(a) {}
};

export struct Program final : ASTNode {
	static constexpr auto KIND = ASTKind::PROGRAM;
	std::span<Decl*> declarations;

	Program() : ASTNode(KIND, 1, 1) {}
};


