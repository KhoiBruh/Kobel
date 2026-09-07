module;

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

export module semantic;

import token;
import logger;

export struct StringHash {
	using is_transparent = void;
	size_t operator()(std::string_view sv) const noexcept {
		return std::hash<std::string_view>{}(sv);
	}
	size_t operator()(const std::string& s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}
	size_t operator()(const char* s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}
};

export template <typename Value>
using StringMap = std::unordered_map<std::string, Value, StringHash, std::equal_to<>>;

export using StringSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

export enum class SemaType {
	I8, I16, I32, I64, ISZ,
	U8, U16, U32, U64, USZ,
	BOOL, CHAR, VOID, STR,
	POINTER, STRUCT, ENUM, ARRAY,
	NULL_TYPE, ERROR_TYPE
};

export struct Type {
	SemaType kind = SemaType::ERROR_TYPE;
	const Type* pointee = nullptr;
	bool is_mut_pointer = false;
	std::string struct_name;
	std::string enum_name;
	const Type* underlying_type = nullptr;
	const Type* element_type = nullptr;
	mutable size_t array_size = 0; // mutable to allow array size inference

	bool is_integer() const {
		switch (kind) {
			case SemaType::I8: case SemaType::I16: case SemaType::I32: case SemaType::I64: case SemaType::ISZ:
			case SemaType::U8: case SemaType::U16: case SemaType::U32: case SemaType::U64: case SemaType::USZ:
				return true;
			default: return false;
		}
	}

	bool is_signed_integer() const {
		switch (kind) {
			case SemaType::I8: case SemaType::I16: case SemaType::I32: case SemaType::I64: case SemaType::ISZ:
				return true;
			default: return false;
		}
	}

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

	bool can_assign_from(const Type* src) const {
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

export using Semantic = const Type*;


