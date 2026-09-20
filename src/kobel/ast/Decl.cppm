module;

#include <span>
#include <string_view>
#include <cstddef>

export module kobel:ast.Decl;

import :ast.Base;
import :ast.Type;
import :ast.Expr;
import :ast.Stmt;

export namespace kobel::ast {

	struct Decl : ASTNode {
		using ASTNode::ASTNode;
		bool is_pub = false;
	};

	struct ModuleDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_MODULE;
		std::span<std::string_view> path;
		std::string_view full_path;

		explicit ModuleDecl(
			const std::span<std::string_view> p,
			const std::string_view fp,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), path(p), full_path(fp) {
		}
	};

	struct UseDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_USE;
		std::span<std::string_view> path;
		std::string_view full_path;
		std::string_view symbol_name;
		std::string_view alias;
		bool is_wildcard = false;

		UseDecl(
			const std::span<std::string_view> p,
			const std::string_view fp,
			const std::string_view sym,
			const std::string_view al,
			const bool wildcard,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c),
			path(p),
			full_path(fp),
			symbol_name(sym),
			alias(al),
			is_wildcard(wildcard) {
		}
	};

	struct Param {
		std::string_view name;
		TypeNode *type = nullptr;
		bool is_mut = false; // true if var self / var param
		bool has_val = false; // true if val self / val param
	};

	struct GenericParam {
		std::string_view name;
		std::span<std::string_view> bounds;
	};

	struct FnDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_FN;
		std::string_view name;
		std::span<GenericParam> type_params;
		std::span<Param> params;
		TypeNode *return_type = nullptr; // nullptr if void
		BlockStmt *body = nullptr; // nullptr if prototype (in extern, trait)

		explicit FnDecl(
			const std::string_view n,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), name(n) {
		}
	};

	struct StructField {
		std::string_view name;
		TypeNode *type = nullptr;
		bool is_pub = false;

		StructField() = default;

		StructField(
			const std::string_view n,
			TypeNode *t,
			const bool p = false
		) : name(n), type(t), is_pub(p) {
		}
	};

	struct StructDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_STRUCT;
		std::string_view name;
		std::span<GenericParam> type_params;
		std::span<StructField> fields;

		explicit StructDecl(
			const std::string_view n,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), name(n) {
		}
	};

	struct TraitDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_TRAIT;
		std::string_view name;
		std::span<GenericParam> type_params;
		std::span<std::string_view> bases;
		std::span<FnDecl *> methods;

		explicit TraitDecl(
			const std::string_view n,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), name(n) {
		}
	};

	struct ImplDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_IMPL;
		std::string_view struct_name;
		std::span<GenericParam> type_params;
		std::string_view trait_name; // empty if inherent impl
		std::span<FnDecl *> methods;

		explicit ImplDecl(
			const std::string_view st_name,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), struct_name(st_name) {
		}
	};

	struct EnumMember {
		std::string_view name;
		Expr *value = nullptr; // nullptr if auto-incremented
		size_t line = 0;
		size_t col = 0;
	};

	struct EnumDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_ENUM;
		std::string_view name;
		TypeNode *underlying_type = nullptr; // nullptr if default i32
		std::span<EnumMember> members;

		explicit EnumDecl(
			const std::string_view n,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), name(n) {
		}
	};

	struct ConstDecl final : Decl {
		static constexpr auto KIND = ASTKind::DECL_CONST;
		std::string_view name;
		TypeNode *type = nullptr;
		Expr *value = nullptr;

		ConstDecl(
			const std::string_view n,
			TypeNode *ty,
			Expr *val,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), name(n), type(ty), value(val) {
		}
	};

	struct ExternBlock final : Decl {
		static constexpr auto KIND = ASTKind::DECL_EXTERN_BLOCK;
		std::string_view abi; // "libc", "C"
		std::span<FnDecl *> declarations;

		explicit ExternBlock(
			const std::string_view a,
			const size_t l = 0,
			const size_t c = 0
		) : Decl(KIND, l, c), abi(a) {
		}
	};

	struct Program final : ASTNode {
		static constexpr auto KIND = ASTKind::PROGRAM;
		std::span<Decl *> declarations;

		Program() : ASTNode(KIND, 1, 1) {
		}
	};

}
