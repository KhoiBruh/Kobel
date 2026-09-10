module;

#include <span>
#include <string_view>

export module ast.type;

import ast.base;

// ============================================================================
// 3. Syntactic Type System (TypeNode)
// ============================================================================

export struct TypeNode : ASTNode {
	using ASTNode::ASTNode;
};

// Named type: "i32", "u8", "usz", "bool", "char", "MyStruct", "Box<i32>"
export struct NamedType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_NAMED;
	std::string_view name;
	std::span<TypeNode *> type_args;

	explicit NamedType(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), name(n), type_args() {
	}

	NamedType(
		const std::string_view n,
		std::span<TypeNode *> args,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), name(n), type_args(args) {
	}
};

// Pointer type: *T (read-only) or &T (read-write)
export struct PointerType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_POINTER;
	bool is_mut; // true: &T, false: *T
	TypeNode *pointee;

	PointerType(
		const bool mut,
		TypeNode *p,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), is_mut(mut), pointee(p) {
	}
};

// Static array type: Array<T> or Array<T>(N)
export struct ArrayType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_ARRAY;
	TypeNode *element_type;
	size_t size = 0; // 0 if inferred from initializer

	explicit ArrayType(
		TypeNode *elem,
		const size_t sz = 0,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), element_type(elem), size(sz) {
	}
};
