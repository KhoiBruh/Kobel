module;

#include <memory>
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
// 6. Khai báo Cấp cao (Decl)
// ============================================================================

export struct Decl : ASTNode {
	using ASTNode::ASTNode;
};

export struct Param {
	std::string_view name;
	std::unique_ptr<TypeNode> type;
	bool is_mut = false; // true nếu là var self / var param
	bool has_val = false; // true nếu là val self / val param
};

// Hàm thuần túy: fn name(a: i32, b: i32): i32 { ... }
export struct FnDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_FN;
	std::string_view name;
	std::vector<Param> params;
	std::unique_ptr<TypeNode> return_type; // nullptr nếu void
	std::unique_ptr<BlockStmt> body;       // nullptr nếu prototype (trong extern)

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

// Struct dữ liệu và phương thức: struct Point(x: i32, y: i32) { ... }
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
	std::unique_ptr<Expr> value; // nullptr nếu tự tăng
	size_t line = 0;
	size_t col = 0;
};

// Enum liệt kê: enum Status : u16 { A, B = 504, C }
export struct EnumDecl final : Decl {
	static constexpr auto KIND = ASTKind::DECL_ENUM;
	std::string_view name;
	std::unique_ptr<TypeNode> underlying_type; // nullptr nếu mặc định i32
	std::vector<EnumMember> members;

	explicit EnumDecl(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : Decl(KIND, l, c), name(n) {}
};

// Hằng số top-level: const MAX_SIZE: i32 = 100;
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

// Khai báo FFI: extern "libc" { fn printf(fmt: *char): i32; }
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
