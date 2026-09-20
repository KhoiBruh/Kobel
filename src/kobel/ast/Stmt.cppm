module;

#include <span>
#include <string_view>
#include <cstddef>

export module kobel:ast.Stmt;

import :ast.Base;
import :ast.Type;
import :ast.Expr;

export namespace kobel::ast {

	struct Stmt : ASTNode {
		using ASTNode::ASTNode;
	};

	struct BlockStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_BLOCK;
		std::span<Stmt *> statements;

		explicit BlockStmt(
			const std::span<Stmt *> stmts,
			const size_t l = 0,
			const size_t c = 0
		) : Stmt(KIND, l, c), statements(stmts) {
		}
	};

	struct ExprStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_EXPR;
		Expr *expr;

		explicit ExprStmt(
			Expr *e,
			const size_t l = 0,
			const size_t c = 0
		) : Stmt(KIND, l, c), expr(e) {
		}
	};

	struct VarDeclStmt final : Stmt {
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
			is_mut(mut),
			name(n),
			type_annotation(ty),
			initializer(init) {
		}
	};

	struct IfStmt final : Stmt {
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

	struct WhileStmt final : Stmt {
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

	struct ReturnStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_RETURN;
		Expr *value; // nullptr if return void;

		explicit ReturnStmt(
			Expr *val = nullptr,
			const size_t l = 0,
			const size_t c = 0
		) : Stmt(KIND, l, c), value(val) {
		}
	};

	struct BreakStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_BREAK;

		explicit BreakStmt(const size_t l = 0, const size_t c = 0)
			: Stmt(KIND, l, c) {
		}
	};

	struct ContinueStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_CONTINUE;

		explicit ContinueStmt(const size_t l = 0, const size_t c = 0)
			: Stmt(KIND, l, c) {
		}
	};

	struct WhenStmtArm {
		std::span<Expr *> patterns; // empty if is_else
		bool is_else = false;
		Stmt *body = nullptr;
		size_t line = 0;
		size_t col = 0;
	};

	struct WhenStmt final : Stmt {
		static constexpr auto KIND = ASTKind::STMT_WHEN;
		Expr *condition; // nullptr if when { ... }
		std::span<WhenStmtArm> arms;

		WhenStmt(
			Expr *cond,
			const std::span<WhenStmtArm> a,
			const size_t l = 0,
			const size_t c = 0
		) : Stmt(KIND, l, c), condition(cond), arms(a) {
		}
	};

}
