module;

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module semantic;

import token;
import logger;

export struct StringHash {
	using is_transparent = void;

	size_t operator()(const std::string_view sv) const noexcept {
		return std::hash<std::string_view>{}(sv);
	}

	size_t operator()(const std::string &s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}

	size_t operator()(const char *s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}
};

export template<typename Value>
using StringMap = std::unordered_map<std::string, Value, StringHash, std::equal_to<> >;

export using StringSet = std::unordered_set<std::string, StringHash, std::equal_to<> >;

export enum class SemaType : uint8_t {
	I8, I16, I32, I64, ISZ,
	U8, U16, U32, U64, USZ,
	F32, F64,
	BOOL, CHAR, VOID, STR,
	NULL_TYPE, ERROR_TYPE,
	POINTER, STRUCT, ENUM, ARRAY
};

export struct Type {
	SemaType kind = SemaType::ERROR_TYPE;
	const Type *pointee = nullptr;
	bool is_mut_pointer = false;
	std::string struct_name;
	std::string enum_name;
	const Type *underlying_type = nullptr;
	const Type *element_type = nullptr;
	mutable size_t array_size = 0; // mutable to allow array size inference

	bool is_integer() const {
		switch (kind) {
			case SemaType::I8:
			case SemaType::I16:
			case SemaType::I32:
			case SemaType::I64:
			case SemaType::ISZ:
			case SemaType::U8:
			case SemaType::U16:
			case SemaType::U32:
			case SemaType::U64:
			case SemaType::USZ:
				return true;
			default: return false;
		}
	}

	bool is_signed_integer() const {
		switch (kind) {
			case SemaType::I8:
			case SemaType::I16:
			case SemaType::I32:
			case SemaType::I64:
			case SemaType::ISZ:
				return true;
			default: return false;
		}
	}

	bool is_float() const { return kind == SemaType::F32 || kind == SemaType::F64; }
	bool is_pointer() const { return kind == SemaType::POINTER; }
	bool is_bool() const { return kind == SemaType::BOOL; }
	bool is_char() const { return kind == SemaType::CHAR; }
	bool is_void() const { return kind == SemaType::VOID; }
	bool is_null() const { return kind == SemaType::NULL_TYPE; }
	bool is_error() const { return kind == SemaType::ERROR_TYPE; }
	bool is_struct() const { return kind == SemaType::STRUCT; }
	bool is_enum() const { return kind == SemaType::ENUM; }
	bool is_array() const { return kind == SemaType::ARRAY; }
	bool is_str() const { return kind == SemaType::STR; }

	bool can_assign_from(const Type *src) const {
		if (kind == SemaType::ERROR_TYPE || src->kind == SemaType::ERROR_TYPE) return true;
		if (is_pointer() && src->is_null()) return true;
		if (is_str() && src->is_str()) return true;
		if (is_pointer() && !is_mut_pointer && pointee && pointee->is_char() && src->is_str()) return true;

		if (is_pointer() && src->is_pointer()) {
			if (is_mut_pointer && !src->is_mut_pointer) return false;
			return pointee == src->pointee;
		}

		if (kind == SemaType::ARRAY && src->kind == SemaType::ARRAY) {
			if (array_size != 0 && array_size != src->array_size) return false;
			return element_type == src->element_type;
		}
		return this == src;
	}

	std::string to_string() const {
		switch (kind) {
			case SemaType::I8: return "i8";
			case SemaType::I16: return "i16";
			case SemaType::I32: return "i32";
			case SemaType::I64: return "i64";
			case SemaType::ISZ: return "isz";
			case SemaType::U8: return "u8";
			case SemaType::U16: return "u16";
			case SemaType::U32: return "u32";
			case SemaType::U64: return "u64";
			case SemaType::USZ: return "usz";
			case SemaType::F32: return "f32";
			case SemaType::F64: return "f64";
			case SemaType::BOOL: return "bool";
			case SemaType::CHAR: return "char";
			case SemaType::VOID: return "void";
			case SemaType::STR: return "str";
			case SemaType::NULL_TYPE: return "null";
			case SemaType::POINTER:
				return (is_mut_pointer ? "&" : "*") + (pointee ? pointee->to_string() : "unknown");
			case SemaType::STRUCT: return struct_name;
			case SemaType::ENUM: return enum_name;
			case SemaType::ARRAY:
				return "Array<" + (element_type ? element_type->to_string() : "unknown") + ">(" +
					   std::to_string(array_size) + ")";
			case SemaType::ERROR_TYPE: return "<error-type>";
		}
		return "<unknown>";
	}
};

export using Semantic = const Type *;

export inline const std::array<Type, 18> primitive_types = [] {
	std::array<Type, 18> arr{};
	for (size_t i = 0; i < 18; ++i) {
		arr[i].kind = static_cast<SemaType>(i);
	}
	return arr;
}();

export struct TypeContext {
	std::vector<std::unique_ptr<Type> > interned_types;

	TypeContext() = default;

	TypeContext(TypeContext &&) noexcept = default;

	TypeContext &operator=(TypeContext &&) noexcept = default;

	TypeContext(const TypeContext &) = delete;

	TypeContext &operator=(const TypeContext &) = delete;

	[[nodiscard]] size_t size() const noexcept { return interned_types.size(); }
	[[nodiscard]] bool empty() const noexcept { return interned_types.empty(); }
	void clear() noexcept { interned_types.clear(); }

	Semantic make_primitive(SemaType k) const {
		if (const auto idx = static_cast<size_t>(k); idx < primitive_types.size()) {
			return &primitive_types[idx];
		}
		return &primitive_types[static_cast<size_t>(SemaType::ERROR_TYPE)];
	}

	Semantic make_pointer(Semantic target, bool mut = false) {
		for (const auto &t: interned_types) {
			if (
				t->kind == SemaType::POINTER &&
				t->pointee == target &&
				t->is_mut_pointer == mut
			)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::POINTER;
		t->pointee = target;
		t->is_mut_pointer = mut;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_struct(std::string_view name) {
		for (const auto &t: interned_types) {
			if (t->kind == SemaType::STRUCT && t->struct_name == name)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::STRUCT;
		t->struct_name = std::string(name);
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_enum(std::string_view name, Semantic under) {
		for (const auto &t: interned_types) {
			if (t->kind == SemaType::ENUM && t->enum_name == name && t->underlying_type == under)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::ENUM;
		t->enum_name = std::string(name);
		t->underlying_type = under;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_array(Semantic elem, size_t sz = 0) {
		for (const auto &t: interned_types) {
			if (t->kind == SemaType::ARRAY && t->element_type == elem && t->array_size == sz)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::ARRAY;
		t->element_type = elem;
		t->array_size = sz;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_void() const { return make_primitive(SemaType::VOID); }
	Semantic make_null() const { return make_primitive(SemaType::NULL_TYPE); }
	Semantic make_error() const { return make_primitive(SemaType::ERROR_TYPE); }
	Semantic make_str() const { return make_primitive(SemaType::STR); }
};
