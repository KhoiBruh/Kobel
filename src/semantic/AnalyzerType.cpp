module;

#include <string>
#include <string_view>
#include <utility>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

Semantic Analyzer::resolve_type(const TypeNode *node) {
	if (!node) return Semantic::make_primitive(SemaType::VOID);

	if (isa<NamedType>(node)) {
		const auto *named = as<NamedType>(node);
		const auto name = named->name;

		if (name == "i8") return Semantic::make_primitive(SemaType::I8);
		if (name == "i16") return Semantic::make_primitive(SemaType::I16);
		if (name == "i32") return Semantic::make_primitive(SemaType::I32);
		if (name == "i64") return Semantic::make_primitive(SemaType::I64);
		if (name == "isz") return Semantic::make_primitive(SemaType::ISZ);

		if (name == "u8") return Semantic::make_primitive(SemaType::U8);
		if (name == "u16") return Semantic::make_primitive(SemaType::U16);
		if (name == "u32") return Semantic::make_primitive(SemaType::U32);
		if (name == "u64") return Semantic::make_primitive(SemaType::U64);
		if (name == "usz") return Semantic::make_primitive(SemaType::USZ);

		if (name == "bool") return Semantic::make_primitive(SemaType::BOOL);
		if (name == "char") return Semantic::make_primitive(SemaType::CHAR);
		if (name == "void") return Semantic::make_primitive(SemaType::VOID);

		// Kiểm tra struct đã khai báo
		std::string resolved_st = resolve_struct_name(name, node->line, node->col);
		if (!resolved_st.empty()) {
			return Semantic::make_struct(resolved_st);
		}

		// Kiểm tra enum đã khai báo
		std::string resolved_enum = resolve_enum_name(name, node->line, node->col);
		if (!resolved_enum.empty()) {
			return Semantic::make_enum(resolved_enum, enums.at(resolved_enum).underlying_type);
		}

		logger.error(node->line, node->col, "Không tìm thấy kiểu dữ liệu '" + std::string(name) + "'");
		return Semantic::make_error();
	}

	if (isa<PointerType>(node)) {
		const auto *ptr = as<PointerType>(node);
		auto pointee_type = resolve_type(ptr->pointee.get());
		return Semantic::make_pointer(std::move(pointee_type), ptr->is_mut);
	}

	if (isa<ArrayType>(node)) {
		const auto *arr = as<ArrayType>(node);
		auto elem_type = resolve_type(arr->element_type.get());
		return Semantic::make_array(std::move(elem_type), arr->size);
	}

	logger.error(node->line, node->col, "Kiểu dữ liệu cú pháp không hợp lệ");
	return Semantic::make_error();
}
