module;

#include <memory>
#include <string_view>
#include <utility>

export module ast.type;

import ast.base;

// ============================================================================
// 3. Hệ thống Kiểu Cú pháp (TypeNode)
// ============================================================================

export struct TypeNode : ASTNode {
	using ASTNode::ASTNode;
};

// Kiểu định danh đơn: "i32", "u8", "usz", "bool", "char", "MyStruct"
export struct NamedType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_NAMED;
	std::string_view name;

	explicit NamedType(
		const std::string_view n,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), name(n) {}
};

// Kiểu con trỏ: *T (chỉ đọc) hoặc &T (đọc/ghi)
export struct PointerType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_POINTER;
	bool is_mut; // true: &T, false: *T
	std::unique_ptr<TypeNode> pointee;

	PointerType(
		const bool mut,
		std::unique_ptr<TypeNode> p,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), is_mut(mut), pointee(std::move(p)) {}
};

// Kiểu mảng tĩnh: Array<T> hoặc Array<T>(N)
export struct ArrayType final : TypeNode {
	static constexpr auto KIND = ASTKind::TYPE_ARRAY;
	std::unique_ptr<TypeNode> element_type;
	size_t size = 0; // 0 nếu suy luận từ initializer

	ArrayType(
		std::unique_ptr<TypeNode> elem,
		const size_t sz = 0,
		const size_t l = 0,
		const size_t c = 0
	) : TypeNode(KIND, l, c), element_type(std::move(elem)), size(sz) {}
};

