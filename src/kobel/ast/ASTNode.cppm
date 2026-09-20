module;

#include <cstdint>

export module kobel:ast.Base;

import :ast.ASTKind;

export namespace kobel::ast {

	struct ASTNode {
		ASTKind kind;
		size_t line = 0;
		size_t col = 0;

		explicit ASTNode(
			const ASTKind kind,
			const size_t line = 0,
			const size_t col = 0
		) : kind(kind), line(line), col(col) {
		}

		virtual ~ASTNode() = default;
	};

	template<typename T>
	bool isa(const ASTNode *node) {
		return node && node->kind == T::KIND;
	}

	template<typename T>
	T *as(ASTNode *node) {
		return isa<T>(node) ? static_cast<T *>(node) : nullptr;
	}

	template<typename T>
	const T *as(const ASTNode *node) {
		return isa<T>(node) ? static_cast<const T *>(node) : nullptr;
	}

}
