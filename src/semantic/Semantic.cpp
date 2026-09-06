module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>

export module semantic;

import token;
import logger;

export enum class SemaType {
	// Số nguyên có dấu
	I8, I16, I32, I64, ISZ,
	// Số nguyên không dấu
	U8, U16, U32, U64, USZ,
	// Kiểu cơ sở khác
	BOOL, CHAR, VOID,
	// Con trỏ, Struct, Enum & Mảng
	POINTER,
	STRUCT,
	ENUM,
	ARRAY,
	// Kiểu đặc biệt
	NULL_TYPE,
	ERROR_TYPE
};

export struct Semantic {
	SemaType kind = SemaType::ERROR_TYPE;
	std::shared_ptr<Semantic> pointee = nullptr; // nếu là POINTER
	bool is_mut_pointer = false; // true cho &T, false cho *T
	std::string struct_name; // nếu là STRUCT
	std::string enum_name; // nếu là ENUM
	std::shared_ptr<Semantic> underlying_type = nullptr; // nếu là ENUM
	std::shared_ptr<Semantic> element_type = nullptr; // nếu là ARRAY
	size_t array_size = 0; // nếu là ARRAY

	static Semantic make_primitive(const SemaType k) {
		Semantic t;
		t.kind = k;
		return t;
	}

	static Semantic make_pointer(Semantic target, const bool mut = false) {
		Semantic t;
		t.kind = SemaType::POINTER;
		t.pointee = std::make_shared<Semantic>(std::move(target));
		t.is_mut_pointer = mut;
		return t;
	}

	static Semantic make_struct(const std::string_view name) {
		Semantic t;
		t.kind = SemaType::STRUCT;
		t.struct_name = std::string(name);
		return t;
	}

	static Semantic make_enum(const std::string_view name, Semantic under = make_primitive(SemaType::I32)) {
		Semantic t;
		t.kind = SemaType::ENUM;
		t.enum_name = std::string(name);
		t.underlying_type = std::make_shared<Semantic>(std::move(under));
		return t;
	}

	static Semantic make_array(Semantic elem, const size_t sz = 0) {
		Semantic t;
		t.kind = SemaType::ARRAY;
		t.element_type = std::make_shared<Semantic>(std::move(elem));
		t.array_size = sz;
		return t;
	}

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
			if (!pointee || !other.pointee) return false;
			return pointee->equals(*other.pointee);
		}
		if (kind == SemaType::STRUCT) return struct_name == other.struct_name;
		if (kind == SemaType::ENUM) return enum_name == other.enum_name;
		if (kind == SemaType::ARRAY) {
			if (array_size != other.array_size) return false;
			if (!element_type || !other.element_type) return false;
			return element_type->equals(*other.element_type);
		}
		return true;
	}

	bool can_assign_from(const Semantic &src) const {
		if (kind == SemaType::ERROR_TYPE || src.kind == SemaType::ERROR_TYPE) return true;
		// Con trỏ có thể nhận giá trị null
		if (is_pointer() && src.is_null()) return true;
		// Mảng: nếu kích thước đích là 0 (suy luận kích thước), chỉ cần khớp kiểu phần tử
		if (kind == SemaType::ARRAY && src.kind == SemaType::ARRAY) {
			if (array_size != 0 && array_size != src.array_size) return false;
			return element_type && src.element_type && element_type->equals(*src.element_type);
		}
		// Bắt buộc khớp kiểu chính xác (Strict Typing)
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
			case SemaType::POINTER: return (is_mut_pointer ? "&" : "*") + (pointee ? pointee->to_string() : "unknown");
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
