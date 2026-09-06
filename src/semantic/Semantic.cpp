module;

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
	// Signed integers
	I8, I16, I32, I64, ISZ,
	// Unsigned integers
	U8, U16, U32, U64, USZ,
	// Other primitive types
	BOOL, CHAR, VOID,
	// Pointer, Struct, Enum & Array
	POINTER,
	STRUCT,
	ENUM,
	ARRAY,
	// Special types
	NULL_TYPE,
	ERROR_TYPE
};

export struct Semantic;

export struct TypePayload {
	std::shared_ptr<Semantic> pointee = nullptr;
	bool is_mut_pointer = false;
	std::string struct_name;
	std::string enum_name;
	std::shared_ptr<Semantic> underlying_type = nullptr;
	std::shared_ptr<Semantic> element_type = nullptr;
	size_t array_size = 0;
};

export struct Semantic {
	SemaType kind = SemaType::ERROR_TYPE;
	std::shared_ptr<TypePayload> payload = nullptr;

	std::shared_ptr<Semantic> pointee() const {
		return payload ? payload->pointee : nullptr;
	}
	bool is_mut_pointer() const {
		return payload ? payload->is_mut_pointer : false;
	}
	const std::string& struct_name() const {
		static const std::string empty;
		return payload ? payload->struct_name : empty;
	}
	const std::string& enum_name() const {
		static const std::string empty;
		return payload ? payload->enum_name : empty;
	}
	std::shared_ptr<Semantic> underlying_type() const {
		return payload ? payload->underlying_type : nullptr;
	}
	std::shared_ptr<Semantic> element_type() const {
		return payload ? payload->element_type : nullptr;
	}
	size_t array_size() const {
		return payload ? payload->array_size : 0;
	}
	void set_array_size(const size_t sz) {
		if (payload) payload->array_size = sz;
	}

	static Semantic make_primitive(const SemaType k) {
		Semantic t;
		t.kind = k;
		t.payload = nullptr;
		return t;
	}

	static Semantic make_pointer(Semantic target, const bool mut = false) {
		Semantic t;
		t.kind = SemaType::POINTER;
		t.payload = std::make_shared<TypePayload>();
		t.payload->pointee = std::make_shared<Semantic>(std::move(target));
		t.payload->is_mut_pointer = mut;
		return t;
	}

	static Semantic make_struct(const std::string_view name) {
		Semantic t;
		t.kind = SemaType::STRUCT;
		t.payload = std::make_shared<TypePayload>();
		t.payload->struct_name = std::string(name);
		return t;
	}

	static Semantic make_enum(const std::string_view name, Semantic under = make_primitive(SemaType::I32)) {
		Semantic t;
		t.kind = SemaType::ENUM;
		t.payload = std::make_shared<TypePayload>();
		t.payload->enum_name = std::string(name);
		t.payload->underlying_type = std::make_shared<Semantic>(std::move(under));
		return t;
	}

	static Semantic make_array(Semantic elem, const size_t sz = 0) {
		Semantic t;
		t.kind = SemaType::ARRAY;
		t.payload = std::make_shared<TypePayload>();
		t.payload->element_type = std::make_shared<Semantic>(std::move(elem));
		t.payload->array_size = sz;
		return t;
	}

	static Semantic make_void() { return make_primitive(SemaType::VOID); }
	static Semantic make_null() { return make_primitive(SemaType::NULL_TYPE); }
	static Semantic make_error() { return make_primitive(SemaType::ERROR_TYPE); }

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
			default:
				return false;
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
			default:
				return false;
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

	bool equals(const Semantic &other) const {
		if (kind == SemaType::ERROR_TYPE || other.kind == SemaType::ERROR_TYPE) return true;
		if (kind != other.kind) return false;
		if (kind == SemaType::POINTER) {
			if (!payload || !other.payload) return false;
			if (!payload->pointee || !other.payload->pointee) return false;
			return payload->is_mut_pointer == other.payload->is_mut_pointer &&
			       payload->pointee->equals(*other.payload->pointee);
		}
		if (kind == SemaType::STRUCT) {
			return struct_name() == other.struct_name();
		}
		if (kind == SemaType::ENUM) {
			return enum_name() == other.enum_name();
		}
		if (kind == SemaType::ARRAY) {
			if (array_size() != other.array_size()) return false;
			if (!payload || !other.payload) return false;
			if (!payload->element_type || !other.payload->element_type) return false;
			return payload->element_type->equals(*other.payload->element_type);
		}
		return true;
	}

	bool can_assign_from(const Semantic &src) const {
		if (kind == SemaType::ERROR_TYPE || src.kind == SemaType::ERROR_TYPE) return true;
		// Pointers can accept null
		if (is_pointer() && src.is_null()) return true;

		// Pointer compatibility:
		// &T (mutable pointer) can be assigned to *T (const/raw pointer) (safe covariant decay)
		// *T cannot be assigned to &T (loss of const safety)
		if (is_pointer() && src.is_pointer()) {
			if (is_mut_pointer() && !src.is_mut_pointer()) return false;
			if (!payload || !src.payload) return false;
			if (!payload->pointee || !src.payload->pointee) return false;
			return payload->pointee->equals(*src.payload->pointee);
		}

		// Array: if destination size is 0 (size inference), only element types must match
		if (kind == SemaType::ARRAY && src.kind == SemaType::ARRAY) {
			if (array_size() != 0 && array_size() != src.array_size()) return false;
			return element_type() && src.element_type() && element_type()->equals(*src.element_type());
		}
		// Strict typing: types must match exactly
		return equals(src);
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
			case SemaType::NULL_TYPE: return "null";
			case SemaType::POINTER:
				return (is_mut_pointer() ? "&" : "*") + (pointee() ? pointee()->to_string() : "unknown");
			case SemaType::STRUCT: return struct_name();
			case SemaType::ENUM: return enum_name();
			case SemaType::ARRAY:
				return "Array<" + (element_type() ? element_type()->to_string() : "unknown") + ">(" +
					std::to_string(array_size()) + ")";
			case SemaType::ERROR_TYPE: return "<error-type>";
		}
		return "<unknown>";
	}
};
