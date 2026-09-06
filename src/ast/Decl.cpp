module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
	std::vector<std::string_view> path;
	std::string full_path;

	explicit ModuleDecl(
		std::vector<std::string_view> p,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), path(std::move(p)) {
		for (size_t i = 0; i < path.size(); ++i) {
			if (i > 0) full_path += ".";
			full_path += path[i];
		}
	}
};

// Use declaration: use a.b.c.A; or use a.b.c.A as B; or use a.b.c.*;
export struct UseDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_USE;
	std::vector<std::string_view> path;
	std::string full_path;
	std::string_view symbol_name;
	std::string_view alias;
	bool is_wildcard = false;

	UseDecl(
		std::vector<std::string_view> p,
		const std::string_view sym,
		const std::string_view al,
		const bool wildcard,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), path(std::move(p)), symbol_name(sym), alias(al), is_wildcard(wildcard) {
		for (size_t i = 0; i < path.size(); ++i) {
			if (i > 0) full_path += ".";
			full_path += path[i];
		}
	}
};

export struct Param {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
	bool is_mut = false; // true if var self / var param
	bool has_val = false; // true if val self / val param
};

// Function declaration: fn name(a: i32, b: i32): i32 { ... }
export struct FnDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_FN;
	std::string_view name;
	std::vector<Param> params;
	std::unique_ptr<TypeNode> return_type; // nullptr if void
	std::unique_ptr<BlockStmt> body;       // nullptr if prototype (in extern)

	explicit FnDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

export struct StructField {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
};

// Struct declaration: struct Point(x: i32, y: i32) { ... }
export struct StructDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_STRUCT;
	std::string_view name;
	std::vector<StructField> fields;
	std::vector<std::unique_ptr<FnDecl>> methods;

	explicit StructDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

export struct EnumMember {
	std::string_view name;
	std::unique_ptr<Expr> value; // nullptr if auto-incremented
	size_t line = 0;
	size_t col = 0;
};

// Enum declaration: enum Status : u16 { A, B = 504, C }
export struct EnumDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_ENUM;
	std::string_view name;
	std::unique_ptr<TypeNode> underlying_type; // nullptr if default i32
	std::vector<EnumMember> members;

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
	std::unique_ptr<TypeNode> type;
	std::unique_ptr<Expr> value;

	ConstDecl(
		const std::string_view n,
		std::unique_ptr<TypeNode> ty,
		std::unique_ptr<Expr> val,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n), type(std::move(ty)), value(std::move(val)) {}
};

// FFI declaration: extern "libc" { fn printf(fmt: *char): i32; }
export struct ExternBlock final : Decl {
	static constexpr auto KIND = ASTKind::DECL_EXTERN_BLOCK;
	std::string_view abi; // "libc", "C"
	std::vector<std::unique_ptr<FnDecl>> declarations;

	explicit ExternBlock(
		const std::string_view a,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), abi(a) {}
};

export struct Program final : ASTNode {
	static constexpr auto KIND = ASTKind::PROGRAM;
	std::vector<std::unique_ptr<Decl>> declarations;

	Program() : ASTNode(KIND, 1, 1) {}
};
