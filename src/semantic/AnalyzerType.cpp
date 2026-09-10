module;

#include <string>
#include <string_view>
#include <utility>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

Semantic Analyzer::resolve_type_by_name(const std::string_view name, const size_t line, const size_t col) {
	if (auto it = active_type_substitutions.find(name); it != active_type_substitutions.end()) {
		return it->second;
	}

	if (name == "i8") return make_primitive(SemaType::I8);
	if (name == "i16") return make_primitive(SemaType::I16);
	if (name == "i32") return make_primitive(SemaType::I32);
	if (name == "i64") return make_primitive(SemaType::I64);
	if (name == "isz") return make_primitive(SemaType::ISZ);

	if (name == "u8") return make_primitive(SemaType::U8);
	if (name == "u16") return make_primitive(SemaType::U16);
	if (name == "u32") return make_primitive(SemaType::U32);
	if (name == "u64") return make_primitive(SemaType::U64);
	if (name == "usz") return make_primitive(SemaType::USZ);

	if (name == "bool") return make_primitive(SemaType::BOOL);
	if (name == "char") return make_primitive(SemaType::CHAR);
	if (name == "void") return make_primitive(SemaType::VOID);
	if (name == "str") return make_str();

	const auto resolved_st = resolve_struct_name(name, line, col);
	if (!resolved_st.empty()) return make_struct(resolved_st);

	const auto resolved_enum = resolve_enum_name(name, line, col);
	if (!resolved_enum.empty()) return make_enum(resolved_enum, enums.at(resolved_enum).underlying_type);

	return nullptr;
}

Semantic Analyzer::substitute_type(const TypeNode *node, const StringMap<Semantic> &type_map) {
	if (!node) return make_primitive(SemaType::VOID);

	if (isa<NamedType>(node)) {
		const auto *named = as<NamedType>(node);

		// Check if it's a type parameter (e.g. "T" -> i32)
		auto it = type_map.find(named->name);
		if (it != type_map.end()) {
			return it->second;
		}

		// Generic type inside struct (e.g. Box<T>)
		if (!named->type_args.empty()) {
			std::vector<Semantic> sub_args;
			for (const auto *arg : named->type_args) {
				sub_args.push_back(substitute_type(arg, type_map));
			}

			std::string gen_name = resolve_generic_struct_name(named->name, node->line, node->col);
			if (gen_name.empty()) {
				logger.error(node->line, node->col, "Unknown generic struct '" + std::string(named->name) + "'");
				return make_error();
			}

			const auto *generic_st = generic_structs.at(gen_name);
			std::string inst_name = std::string(named->name) + "<";
			for (size_t i = 0; i < sub_args.size(); ++i) {
				if (i > 0) inst_name += ", ";
				inst_name += sub_args[i]->to_string();
			}
			inst_name += ">";

			if (structs.contains(inst_name)) {
				return make_struct(inst_name);
			}

			return instantiate_struct(generic_st, inst_name, sub_args, node->line, node->col);
		}

		// Concrete type (e.g. i32, str, Point)
		if (const auto ty = resolve_type_by_name(named->name, node->line, node->col)) {
			return ty;
		}

		logger.error(node->line, node->col, "Unknown type '" + std::string(named->name) + "'");
		return make_error();
	}

	if (isa<PointerType>(node)) {
		const auto *ptr = as<PointerType>(node);
		auto pointee_type = substitute_type(ptr->pointee, type_map);
		return make_pointer(std::move(pointee_type), ptr->is_mut);
	}

	if (isa<ArrayType>(node)) {
		const auto *arr = as<ArrayType>(node);
		auto elem_type = substitute_type(arr->element_type, type_map);
		return make_array(std::move(elem_type), arr->size);
	}

	return make_error();
}

Semantic Analyzer::instantiate_struct(
	const StructDecl *generic_st,
	const std::string &instantiated_name,
	const std::vector<Semantic> &type_args,
	const size_t line,
	const size_t col
) {
	if (!generic_st) return make_error();

	if (structs.contains(instantiated_name)) {
		return make_struct(instantiated_name);
	}

	if (type_args.size() != generic_st->type_params.size()) {
		logger.error(
			line, col,
			"Generic struct '" + std::string(generic_st->name) + "' expects " +
			std::to_string(generic_st->type_params.size()) + " type arguments, but got " +
			std::to_string(type_args.size())
		);
		return make_error();
	}

	StringMap<Semantic> type_map;
	for (size_t i = 0; i < generic_st->type_params.size(); ++i) {
		type_map[std::string(generic_st->type_params[i].name)] = type_args[i];
	}

	std::string mod = get_decl_module(generic_st);
	StructSymbol sym = {
		.name = instantiated_name,
		.is_pub = generic_st->is_pub,
		.module_name = mod,
		.line = generic_st->line,
		.col = generic_st->col
	};

	structs[instantiated_name] = sym;
	if (!mod.empty()) {
		structs[to_llvm_name(instantiated_name)] = sym;
	}
	instantiated_struct_order.push_back(instantiated_name);
	instantiated_type_maps[instantiated_name] = type_map;

	// Resolve fields under substitution
	for (const auto &[f_name_sv, f_type_node] : generic_st->fields) {
		auto f_name = std::string(f_name_sv);
		auto f_type = substitute_type(f_type_node, type_map);
		sym.field_types[f_name] = f_type;
		sym.field_order.push_back(f_name);
	}

	// Resolve methods under substitution
	for (const auto &method : generic_st->methods) {
		auto m_name = std::string(method->name);
		std::string mangled_name = to_llvm_name(instantiated_name) + "_" + m_name;
		FnSymbol fn_sym = {
			.name = mangled_name,
			.return_type = substitute_type(method->return_type, type_map),
			.is_pub = method->is_pub,
			.module_name = mod,
			.line = method->line,
			.col = method->col
		};

		for (const auto &[p_name, p_type, is_mut, has_val] : method->params) {
			fn_sym.param_names.push_back(std::string(p_name));
			if (p_name == "self") {
				if (p_type) {
					fn_sym.param_types.push_back(substitute_type(p_type, type_map));
				} else if (is_mut) {
					fn_sym.param_types.push_back(
						make_pointer(make_struct(instantiated_name), true)
					);
				} else if (has_val) {
					fn_sym.param_types.push_back(
						make_pointer(make_struct(instantiated_name), false)
					);
				} else {
					fn_sym.param_types.push_back(make_struct(instantiated_name));
				}
			} else {
				fn_sym.param_types.push_back(substitute_type(p_type, type_map));
			}
		}
		sym.methods[m_name] = fn_sym;
		functions[mangled_name] = fn_sym;
	}

	structs[instantiated_name] = sym;
	if (!mod.empty()) {
		structs[to_llvm_name(instantiated_name)] = sym;
	}

	return make_struct(instantiated_name);
}

Semantic Analyzer::resolve_type(const TypeNode *node) {
	if (!node) return make_primitive(SemaType::VOID);

	if (isa<NamedType>(node)) {
		const auto *named = as<NamedType>(node);

		// 1. Generic type instantiation: Box<i32>, Pair<i32, str>
		if (!named->type_args.empty()) {
			std::vector<Semantic> resolved_args;
			for (const auto *arg : named->type_args) {
				resolved_args.push_back(resolve_type(arg));
			}

			std::string gen_name = resolve_generic_struct_name(named->name, node->line, node->col);
			if (gen_name.empty()) {
				logger.error(node->line, node->col, "Unknown generic struct '" + std::string(named->name) + "'");
				return make_error();
			}

			const auto *generic_st = generic_structs.at(gen_name);
			std::string inst_name = std::string(named->name) + "<";
			for (size_t i = 0; i < resolved_args.size(); ++i) {
				if (i > 0) inst_name += ", ";
				inst_name += resolved_args[i]->to_string();
			}
			inst_name += ">";

			if (structs.contains(inst_name)) {
				return make_struct(inst_name);
			}

			return instantiate_struct(generic_st, inst_name, resolved_args, node->line, node->col);
		}

		// 2. Error if using a generic struct without type arguments
		if (std::string gen_name = resolve_generic_struct_name(named->name); !gen_name.empty()) {
			logger.error(node->line, node->col, "Generic struct '" + std::string(named->name) + "' requires type arguments");
			return make_error();
		}

		// 3. Regular non-generic type
		if (const auto ty = resolve_type_by_name(named->name, node->line, node->col)) {
			return ty;
		}

		logger.error(node->line, node->col, "Unknown type '" + std::string(named->name) + "'");
		return make_error();
	}

	if (isa<PointerType>(node)) {
		const auto *ptr = as<PointerType>(node);
		auto pointee_type = resolve_type(ptr->pointee);
		return make_pointer(std::move(pointee_type), ptr->is_mut);
	}

	if (isa<ArrayType>(node)) {
		const auto *arr = as<ArrayType>(node);
		auto elem_type = resolve_type(arr->element_type);
		return make_array(std::move(elem_type), arr->size);
	}

	logger.error(node->line, node->col, "Invalid type syntax");
	return make_error();
}



