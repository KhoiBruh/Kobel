module;

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

module semantic.analyzer;

import ast;
import token;
import logger;
import semantic;
import semantic.symbol;

namespace {

std::optional<std::string> get_symbol_path(const Expr *expr, Analyzer *analyzer) {
	if (!expr) return std::nullopt;
	if (isa<IdentifierExpr>(expr)) {
		auto id = as<IdentifierExpr>(expr);
		if (analyzer->lookup_variable(id->name)) return std::nullopt;
		return std::string(id->name);
	}
	if (isa<MemberExpr>(expr)) {
		auto m = as<MemberExpr>(expr);
		auto obj_path = get_symbol_path(m->object, analyzer);
		if (!obj_path) return std::nullopt;
		return *obj_path + "." + std::string(m->member);
	}
	return std::nullopt;
}

bool deduce_type_arg(
	const TypeNode *param_type,
	Semantic arg_type,
	const FnDecl *gen_fn,
	StringMap<Semantic> &deduced
) {
	if (!param_type || !arg_type) return false;

	if (isa<NamedType>(param_type)) {
		const auto *named = as<NamedType>(param_type);
		for (const auto &tp : gen_fn->type_params) {
			if (tp.name == named->name) {
				if (named->type_args.empty()) {
					std::string t_name = std::string(named->name);
					if (deduced.contains(t_name)) {
						if (deduced[t_name] != arg_type) {
							return false;
						}
					} else {
						deduced[t_name] = arg_type;
					}
					return true;
				}
				return false;
			}
		}
		return false;
	}

	if (isa<PointerType>(param_type)) {
		const auto *ptr = as<PointerType>(param_type);
		if (arg_type->is_pointer()) {
			return deduce_type_arg(ptr->pointee, arg_type->pointee, gen_fn, deduced);
		}
		return false;
	}

	if (isa<ArrayType>(param_type)) {
		const auto *arr = as<ArrayType>(param_type);
		if (arg_type->is_array()) {
			return deduce_type_arg(arr->element_type, arg_type->element_type, gen_fn, deduced);
		}
		return false;
	}

	return false;
}

} // namespace

Semantic Analyzer::compute_expr_type(const Expr *expr) {
	if (!expr) return make_error();

	// 1. Literal
	if (isa<LiteralExpr>(expr)) {
		switch (const auto *lit = as<LiteralExpr>(expr); lit->literal_kind) {
			case LiteralKind::INT: {
				const auto raw = lit->raw_text;
				bool is_hex = raw.starts_with("0x") || raw.starts_with("0X");
				if (raw.ends_with("UB")) return make_primitive(SemaType::U8);
				if (raw.ends_with("US")) return make_primitive(SemaType::U16);
				if (raw.ends_with("UL")) return make_primitive(SemaType::U64);
				if (raw.ends_with("UZ")) return make_primitive(SemaType::USZ);
				if (raw.ends_with("U")) return make_primitive(SemaType::U32);
				if (raw.ends_with("L")) return make_primitive(SemaType::I64);
				if (raw.ends_with("S")) return make_primitive(SemaType::I16);
				if (raw.ends_with("Z")) return make_primitive(SemaType::ISZ);
				if (!is_hex && raw.ends_with("B")) return make_primitive(SemaType::I8);
				if (is_hex && raw.find('_') != std::string_view::npos) {
					size_t last_us = raw.rfind('_');
					if (raw.substr(last_us + 1) == "B") return make_primitive(SemaType::I8);
				}
				return make_primitive(SemaType::I32); // default int is i32
			}
			case LiteralKind::FLOAT: {
				const auto raw = lit->raw_text;
				if (raw.ends_with("F")) return make_primitive(SemaType::F32);
				return make_primitive(SemaType::F64);
			}
			case LiteralKind::BOOL: return make_primitive(SemaType::BOOL);
			case LiteralKind::CHAR: return make_primitive(SemaType::CHAR);
			case LiteralKind::STRING: return make_str();
			case LiteralKind::NULL_VAL: return make_null();
			default: return make_error();
		}
	}

	// Array literal: [expr1, expr2, ...]
	if (isa<ArrayLiteralExpr>(expr)) {
		const auto *arr_lit = as<ArrayLiteralExpr>(expr);
		if (arr_lit->elements.empty()) {
			logger.error(arr_lit->line, arr_lit->col, "Array literal cannot be empty");
			return make_error();
		}

		auto first_elem_type = analyze_expr(arr_lit->elements[0]);
		for (size_t i = 1; i < arr_lit->elements.size(); ++i) {
			if (
				auto elem_type = analyze_expr(arr_lit->elements[i]);
				first_elem_type != elem_type
			) {
				logger.error(
					arr_lit->elements[i]->line, arr_lit->elements[i]->col,
					"Array elements must have the same type: expected '" +
					first_elem_type->to_string() + "', got '" + elem_type->to_string() + "'"
				);
				return make_error();
			}
		}

		return make_array(first_elem_type, arr_lit->elements.size());
	}

	// 2. Variable / Identifier
	if (isa<IdentifierExpr>(expr)) {
		const auto *id = as<IdentifierExpr>(expr);
		const auto name = id->name;

		// Lookup local variable / parameter
		if (auto *var = lookup_variable(name)) return var->type;

		// Lookup constant
		std::string resolved_c = resolve_const_name(name, id->line, id->col);
		if (!resolved_c.empty()) {
			resolved_symbols[id] = resolved_c;
			return constants.at(resolved_c).type;
		}

		logger.error(id->line, id->col, "Variable or identifier '" + std::string(name) + "' is not declared");
		return make_error();
	}

	// 3. Assignment: target = value
	if (isa<AssignExpr>(expr)) {
		const auto *a = as<AssignExpr>(expr);

		// Check lvalue and immutability
		Semantic target_type = make_error();
		if (isa<IdentifierExpr>(a->target)) {
			const auto *id = as<IdentifierExpr>(a->target);
			auto *var = lookup_variable(id->name);
			if (!var) {
				logger.error(id->line, id->col, "Variable '" + std::string(id->name) + "' is not declared");
				return make_error();
			}
			if (!var->is_mut) {
				logger.error(
					a->line, a->col, "Cannot reassign immutable variable '" + std::string(id->name) +
					                 "' declared with 'val'"
				);
				return make_error();
			}
		} else if (isa<MemberExpr>(a->target)) {
			const auto *m = as<MemberExpr>(a->target);
			auto obj_type = analyze_expr(m->object);
			if (obj_type->is_enum() && m->member == "value") {
				logger.error(a->line, a->col, "Cannot assign to read-only property '.value' of enum");
				return make_error();
			}
			if (obj_type->is_array() && m->member == "len") {
				logger.error(a->line, a->col, "Cannot assign to read-only property '.len' of array");
				return make_error();
			}
			if (obj_type->is_pointer() && !obj_type->is_mut_pointer) {
				logger.error(a->line, a->col, "Cannot modify field through read-only pointer '*'");
				return make_error();
			}
			target_type = analyze_expr(a->target);
		} else if (isa<IndexExpr>(a->target)) {
			const auto *idx = as<IndexExpr>(a->target);
			auto target_obj_type = analyze_expr(idx->target);
			if (target_obj_type->is_str()) {
				logger.error(a->line, a->col, "Cannot modify elements of immutable string 'str'");
				return make_error();
			}
			target_type = analyze_expr(a->target);
		} else if (isa<UnaryExpr>(a->target) && as<UnaryExpr>(a->target)->op == TokenType::STAR) {
			target_type = analyze_expr(a->target); // *ptr = value
		} else {
			logger.error(a->line, a->col, "Left-hand side of assignment is not a valid lvalue");
			return make_error();
		}

		auto val_type = analyze_expr(a->value);
		if (target_type->is_integer() && val_type->is_integer() &&
		    isa<LiteralExpr>(a->value) &&
		    as<LiteralExpr>(a->value)->literal_kind == LiteralKind::INT) {
			val_type = target_type;
			expr_types[a->value] = target_type;
		}

		if (!target_type->can_assign_from(val_type))
			logger.error(
				a->line, a->col, "Cannot assign value of type '" + val_type->to_string() +
				                 "' to target of type '" + target_type->to_string() + "'"
			);

		return target_type;
	}

	// 4. Binary expression: left op right
	if (isa<BinaryExpr>(expr)) {
		const auto *b = as<BinaryExpr>(expr);
		auto left_type = analyze_expr(b->left);
		auto right_type = analyze_expr(b->right);

		if (left_type->is_error() || right_type->is_error()) return make_error();

		switch (b->op) {
			// Arithmetic operators (+, -, *, /, %)
			case TokenType::PLUS:
			case TokenType::MINUS:
			case TokenType::STAR:
			case TokenType::SLASH:
			case TokenType::PERCENT: {
				// str + str → str (concatenation)
				if (b->op == TokenType::PLUS && left_type->is_str() && right_type->is_str()) {
					return make_str();
				}
				if (!left_type->is_integer() || !right_type->is_integer()) {
					logger.error(b->line, b->col, "Arithmetic operators are only applicable to integer types");
					return make_error();
				}
				if (left_type != right_type) {
					logger.error(
						b->line, b->col, "Type mismatch in arithmetic operation: '" +
						                 left_type->to_string() + "' and '" + right_type->to_string() +
						                 "'. Explicit cast with 'as' is required."
					);
					return make_error();
				}
				return left_type;
			}

			// Comparison operators (<, <=, >, >=)
			case TokenType::LESS:
			case TokenType::LESS_EQUAL:
			case TokenType::GREATER:
			case TokenType::GREATER_EQUAL: {
				if (!left_type->is_integer() || !right_type->is_integer()) {
					logger.error(b->line, b->col, "Comparison operators are only applicable to integer types");
					return make_error();
				}
				if (left_type != right_type) {
					logger.error(
						b->line, b->col, "Type mismatch in comparison: '" +
						                 left_type->to_string() + "' and '" + right_type->to_string() + "'"
					);
					return make_error();
				}
				return make_primitive(SemaType::BOOL);
			}

			// Equality operators (==, !=)
			case TokenType::EQUAL_EQUAL:
			case TokenType::BANG_EQUAL: {
				if (left_type->is_pointer() && right_type->is_null()) return make_primitive(SemaType::BOOL);
				if (left_type->is_null() && right_type->is_pointer()) return make_primitive(SemaType::BOOL);
				// str == str / str != str → bool (content comparison)
				if (left_type->is_str() && right_type->is_str()) return make_primitive(SemaType::BOOL);
				if (left_type != right_type) {
					logger.error(
						b->line, b->col, "Cannot compare different types: '" +
						                 left_type->to_string() + "' and '" + right_type->to_string() + "'"
					);
					return make_error();
				}
				return make_primitive(SemaType::BOOL);
			}

			// Logical operators (&&, ||)
			case TokenType::AND_AND:
			case TokenType::OR_OR: {
				if (!left_type->is_bool() || !right_type->is_bool()) {
					logger.error(
						b->line, b->col, "Logical operators '&&'/'||' are only applicable to boolean (bool) type"
					);
					return make_error();
				}
				return make_primitive(SemaType::BOOL);
			}

			default:
				return make_error();
		}
	}

	// 5. Unary operators (-x, !x, *ptr)
	if (isa<UnaryExpr>(expr)) {
		const auto *u = as<UnaryExpr>(expr);
		auto operand_type = analyze_expr(u->operand);
		if (operand_type->is_error()) return make_error();

		switch (u->op) {
			case TokenType::MINUS:
				if (!operand_type->is_signed_integer()) {
					logger.error(u->line, u->col, "Unary '-' operator is only applicable to signed integer types");
					return make_error();
				}
				return operand_type;

			case TokenType::BANG:
				if (!operand_type->is_bool()) {
					logger.error(u->line, u->col, "Logical NOT '!' operator is only applicable to boolean (bool) type");
					return make_error();
				}
				return operand_type;

			case TokenType::STAR: // Dereference: *ptr
				if (!operand_type->is_pointer() || !operand_type->pointee) {
					logger.error(u->line, u->col, "Dereference operator '*' can only be applied to pointer types");
					return make_error();
				}
				return operand_type->pointee;

			default:
				return make_error();
		}
	}

	// 6. Cast expression: expr as TargetType
	if (isa<CastExpr>(expr)) {
		const auto *c = as<CastExpr>(expr);
		auto src_type = analyze_expr(c->expr);
		auto target_type = resolve_type(c->target_type);

		if (src_type->is_error() || target_type->is_error()) return make_error();

		// Check validity of type cast:
		// - Integer -> Integer
		// - Pointer -> Pointer
		// - Integer (isz/usz) -> Pointer or Pointer -> Integer
		// - Char -> Integer or Integer -> Char
		const bool is_int_to_int = src_type->is_integer() && target_type->is_integer();
		const bool is_ptr_to_ptr = src_type->is_pointer() && target_type->is_pointer();
		const bool is_int_ptr_mix = (src_type->is_integer() && target_type->is_pointer()) ||
		                            (src_type->is_pointer() && target_type->is_integer());
		const bool is_char_int_mix = (src_type->is_char() && target_type->is_integer()) ||
		                             (src_type->is_integer() && target_type->is_char());
		const bool is_enum_int_mix = (src_type->is_enum() && target_type->is_integer()) ||
		                             (src_type->is_integer() && target_type->is_enum()) ||
		                             (src_type->is_enum() && target_type->is_enum() && src_type == target_type);
		const bool is_array_to_ptr = src_type->is_array() && target_type->is_pointer() &&
		                             src_type->element_type && target_type->pointee &&
		                             src_type->element_type == target_type->pointee;

		if (!is_int_to_int && !is_ptr_to_ptr && !is_int_ptr_mix && !is_char_int_mix && !is_enum_int_mix && !
		    is_array_to_ptr) {
			logger.error(
				c->line, c->col, "Cannot cast from '" + src_type->to_string() +
				                 "' to '" + target_type->to_string() + "'"
			);
			return make_error();
		}

		return target_type;
	}

	// 7. Function call, struct instantiation or method call: callee(args...)
	if (isa<CallExpr>(expr)) {
		const auto *c = as<CallExpr>(expr);

		// Check for T.size() static type size inquiry (e.g. Point.size(), i32.size(), str.size())
		if (isa<MemberExpr>(c->callee) && as<MemberExpr>(c->callee)->member == "size") {
			const auto *m = as<MemberExpr>(c->callee);
			if (auto path = get_symbol_path(m->object, this)) {
				if (auto type_sem = resolve_type_by_name(*path, m->line, m->col)) {
					if (!c->args.empty()) {
						logger.error(c->line, c->col, "Type size method '.size()' takes no arguments");
						return make_error();
					}
					resolved_type_sizes[c] = type_sem;
					return make_primitive(SemaType::USZ);
				}
			}
		}

		auto sym_path = get_symbol_path(c->callee, this);
		if (sym_path) {
			const auto raw_callee_name = *sym_path;

			// 1. Check generic struct instantiation: Box<i32>(10) or inferred Box(10)
			std::string gen_st_name = resolve_generic_struct_name(raw_callee_name, c->line, c->col);
			if (!gen_st_name.empty()) {
				const auto *gen_st = generic_structs.at(gen_st_name);
				std::vector<Semantic> resolved_type_args;

				if (!c->type_args.empty()) {
					// Explicit type arguments: Box<i32>(10)
					for (const auto *t_arg : c->type_args) {
						resolved_type_args.push_back(resolve_type(t_arg));
					}
				} else {
					// Inferred type arguments: Box(10)
					if (c->args.size() != gen_st->fields.size()) {
						logger.error(
							c->line, c->col,
							"Struct instantiation '" + raw_callee_name + "' expects " +
							std::to_string(gen_st->fields.size()) + " arguments, but got " +
							std::to_string(c->args.size())
						);
						return make_error();
					}

					StringMap<Semantic> deduced;
					for (size_t i = 0; i < c->args.size(); ++i) {
						auto arg_ty = analyze_expr(c->args[i]);
						if (isa<NamedType>(gen_st->fields[i].type)) {
							const auto p_name = as<NamedType>(gen_st->fields[i].type)->name;
							for (const auto &tp : gen_st->type_params) {
								if (tp.name == p_name && !deduced.contains(std::string(p_name))) {
									deduced[std::string(p_name)] = arg_ty;
								}
							}
						}
					}

					for (const auto &tp : gen_st->type_params) {
						auto it_d = deduced.find(tp.name);
						if (it_d != deduced.end()) {
							resolved_type_args.push_back(it_d->second);
						} else {
							logger.error(
								c->line, c->col,
								"Cannot infer type parameter '" + std::string(tp.name) + "' for generic struct '" + raw_callee_name + "'"
							);
							return make_error();
						}
					}
				}

				std::string inst_name = gen_st_name + "<";
				for (size_t i = 0; i < resolved_type_args.size(); ++i) {
					if (i > 0) inst_name += ", ";
					inst_name += resolved_type_args[i]->to_string();
				}
				inst_name += ">";

				instantiate_struct(gen_st, inst_name, resolved_type_args, c->line, c->col);
				resolved_symbols[c] = inst_name;

				const auto &st_sym = structs.at(inst_name);
				if (c->args.size() != st_sym.field_order.size()) {
					logger.error(
						c->line, c->col, "Struct instantiation '" + inst_name + "' expects " +
						                 std::to_string(st_sym.field_order.size()) + " arguments, but got " +
						                 std::to_string(c->args.size())
					);
					return make_struct(inst_name);
				}

				for (size_t i = 0; i < c->args.size(); ++i) {
					auto arg_type = analyze_expr(c->args[i]);
					const auto &field_name = st_sym.field_order[i];
					const auto &expected_type = st_sym.field_types.at(field_name);
					if (expected_type->is_integer() && arg_type->is_integer() &&
					    isa<LiteralExpr>(c->args[i]) &&
					    as<LiteralExpr>(c->args[i])->literal_kind == LiteralKind::INT) {
						arg_type = expected_type;
						expr_types[c->args[i]] = expected_type;
					}
					if (!expected_type->can_assign_from(arg_type)) {
						logger.error(
							c->line, c->col, "Field '" + field_name + "' of struct '" +
							                 inst_name + "' type mismatch: expected '" +
							                 expected_type->to_string() + "', got '" + arg_type->to_string() +
							                 "'"
						);
					}
				}
				return make_struct(inst_name);
			}

			// 2. Check generic function call: id<i32>(42) or id(42)
			std::string gen_fn_name = resolve_generic_function_name(raw_callee_name, c->line, c->col);
			if (!gen_fn_name.empty()) {
				const auto *gen_fn = generic_functions.at(gen_fn_name);
				std::vector<Semantic> resolved_type_args;

				if (!c->type_args.empty()) {
					// Explicit type arguments: id<i32>(42)
					for (const auto *t_arg : c->type_args) {
						resolved_type_args.push_back(resolve_type(t_arg));
					}
				} else {
					// Inferred type arguments: id(42)
					if (c->args.size() != gen_fn->params.size()) {
						logger.error(
							c->line, c->col,
							"Function '" + raw_callee_name + "' expects " +
							std::to_string(gen_fn->params.size()) + " arguments, but got " +
							std::to_string(c->args.size())
						);
						return make_error();
					}

					StringMap<Semantic> deduced;
					for (size_t i = 0; i < c->args.size(); ++i) {
						auto arg_ty = analyze_expr(c->args[i]);
						deduce_type_arg(gen_fn->params[i].type, arg_ty, gen_fn, deduced);
					}

					for (const auto &tp : gen_fn->type_params) {
						auto it_d = deduced.find(tp.name);
						if (it_d != deduced.end()) {
							resolved_type_args.push_back(it_d->second);
						} else {
							logger.error(
								c->line, c->col,
								"Cannot infer type parameter '" + std::string(tp.name) + "' for generic function '" + raw_callee_name + "'"
							);
							return make_error();
						}
					}
				}

				std::string inst_name = gen_fn_name + "<";
				for (size_t i = 0; i < resolved_type_args.size(); ++i) {
					if (i > 0) inst_name += ", ";
					inst_name += resolved_type_args[i]->to_string();
				}
				inst_name += ">";

				instantiate_function(gen_fn, inst_name, resolved_type_args, c->line, c->col);
				resolved_symbols[c] = inst_name;

				const auto &fn_sym = functions.at(inst_name);
				if (c->args.size() != fn_sym.param_types.size()) {
					logger.error(
						c->line, c->col, "Function '" + raw_callee_name + "' expects " +
						                 std::to_string(fn_sym.param_types.size()) + " arguments, but got " +
						                 std::to_string(c->args.size())
					);
					return fn_sym.return_type;
				}

				for (size_t i = 0; i < c->args.size(); ++i) {
					auto arg_type = analyze_expr(c->args[i]);
					if (fn_sym.param_types[i]->is_integer() && arg_type->is_integer() &&
					    isa<LiteralExpr>(c->args[i]) &&
					    as<LiteralExpr>(c->args[i])->literal_kind == LiteralKind::INT) {
						arg_type = fn_sym.param_types[i];
						expr_types[c->args[i]] = fn_sym.param_types[i];
					}
					if (!fn_sym.param_types[i]->can_assign_from(arg_type)) {
						logger.error(
							c->line, c->col, "Argument " + std::to_string(i + 1) + " of function '" +
							                 raw_callee_name + "' type mismatch: expected '" +
							                 fn_sym.param_types[i]->to_string() + "', got '" + arg_type->to_string()
							                 + "'"
						);
					}
				}

				return fn_sym.return_type;
			}

			// 3. Struct instantiation: Point(10, 20)
			std::string resolved_st = resolve_struct_name(raw_callee_name, c->line, c->col);
			if (!resolved_st.empty()) {
				resolved_symbols[c] = resolved_st;
				const auto &st_sym = structs.at(resolved_st);
				if (c->args.size() != st_sym.field_order.size()) {
					logger.error(
						c->line, c->col, "Struct instantiation '" + raw_callee_name + "' expects " +
						                 std::to_string(st_sym.field_order.size()) + " arguments, but got " +
						                 std::to_string(c->args.size())
					);
					return make_struct(resolved_st);
				}

				for (size_t i = 0; i < c->args.size(); ++i) {
					auto arg_type = analyze_expr(c->args[i]);
					const auto &field_name = st_sym.field_order[i];
					const auto &expected_type = st_sym.field_types.at(field_name);
					if (expected_type->is_integer() && arg_type->is_integer() &&
					    isa<LiteralExpr>(c->args[i]) &&
					    as<LiteralExpr>(c->args[i])->literal_kind == LiteralKind::INT) {
						arg_type = expected_type;
						expr_types[c->args[i]] = expected_type;
					}
					if (!expected_type->can_assign_from(arg_type)) {
						logger.error(
							c->line, c->col, "Field '" + field_name + "' of struct '" +
							                 raw_callee_name + "' type mismatch: expected '" +
							                 expected_type->to_string() + "', got '" + arg_type->to_string() +
							                 "'"
						);
					}
				}
				return make_struct(resolved_st);
			}

			// 4. Regular function call
			std::string resolved_fn = resolve_function_name(raw_callee_name, c->line, c->col);
			if (!resolved_fn.empty()) {
				resolved_symbols[c] = resolved_fn;
				const auto &fn_sym = functions.at(resolved_fn);
				if (c->args.size() != fn_sym.param_types.size()) {
					logger.error(
						c->line, c->col, "Function '" + raw_callee_name + "' expects " +
						                 std::to_string(fn_sym.param_types.size()) + " arguments, but got " +
						                 std::to_string(c->args.size())
					);
					return fn_sym.return_type;
				}

				for (size_t i = 0; i < c->args.size(); ++i) {
					auto arg_type = analyze_expr(c->args[i]);
					if (fn_sym.param_types[i]->is_integer() && arg_type->is_integer() &&
					    isa<LiteralExpr>(c->args[i]) &&
					    as<LiteralExpr>(c->args[i])->literal_kind == LiteralKind::INT) {
						arg_type = fn_sym.param_types[i];
						expr_types[c->args[i]] = fn_sym.param_types[i];
					}
					if (!fn_sym.param_types[i]->can_assign_from(arg_type)) {
						logger.error(
							c->line, c->col, "Argument " + std::to_string(i + 1) + " of function '" +
							                 raw_callee_name + "' type mismatch: expected '" +
							                 fn_sym.param_types[i]->to_string() + "', got '" + arg_type->to_string()
							                 + "'"
						);
					}
				}

				return fn_sym.return_type;
			}

			// If it was an identifier and didn't match any function/struct, report error
			if (isa<IdentifierExpr>(c->callee)) {
				logger.error(c->line, c->col, "Function '" + raw_callee_name + "' is not declared");
				return make_error();
			}
			// If it was a MemberExpr, fall through to method call!
		}

		// 7b. Method call: object.method(args...)
		if (isa<MemberExpr>(c->callee)) {
			const auto *m = as<MemberExpr>(c->callee);

			auto obj_type = analyze_expr(m->object);
			if (obj_type->is_error()) return make_error();

			// Built-in str methods
			if (obj_type->is_str()) {
				const auto method_name = std::string(m->member);
				if (method_name == "size" || method_name == "len") {
					if (!c->args.empty()) {
						logger.error(c->line, c->col, "str." + method_name + "() takes no arguments");
					}
					return make_primitive(SemaType::USZ);
				}
				if (method_name == "c_str") {
					if (!c->args.empty()) {
						logger.error(c->line, c->col, "str.c_str() takes no arguments");
					}
					return make_pointer(make_primitive(SemaType::CHAR));
				}
				if (method_name == "slice") {
					if (c->args.empty() || c->args.size() > 2) {
						logger.error(
							c->line, c->col, "str.slice() expects 1 or 2 arguments: slice(start) or slice(start, end)"
						);
						return make_error();
					}
					for (size_t i = 0; i < c->args.size(); ++i) {
						auto arg_ty = analyze_expr(c->args[i]);
						if (arg_ty->is_error()) return make_error();
						if (!arg_ty->is_integer()) {
							logger.error(
								c->args[i]->line, c->args[i]->col,
								"Argument " + std::to_string(i + 1) + " of str.slice() must be an integer"
							);
							return make_error();
						}
					}
					return make_str();
				}
				logger.error(
					c->line, c->col,
					"str type only supports methods '.len()', '.size()', '.c_str()', and '.slice()'"
				);
				return make_error();
			}

			std::string struct_name;
			if (obj_type->is_struct()) {
				struct_name = obj_type->struct_name;
			} else if (obj_type->is_pointer() && obj_type->pointee && obj_type->pointee->is_struct()) {
				struct_name = obj_type->pointee->struct_name;
			} else {
				logger.error(c->line, c->col, "Methods can only be called on structs or struct pointers");
				return make_error();
			}

			auto it_st = structs.find(struct_name);
			if (it_st == structs.end()) {
				logger.error(c->line, c->col, "Struct definition '" + struct_name + "' not found");
				return make_error();
			}

			const auto method_name = std::string(m->member);
			auto it_m = it_st->second.methods.find(m->member);
			if (it_m == it_st->second.methods.end()) {
				logger.error(
					c->line, c->col, "Struct '" + struct_name + "' has no method named '" + method_name + "'"
				);
				return make_error();
			}

			const auto &method_sym = it_m->second;
			if (!method_sym.is_pub && method_sym.module_name != current_module && !method_sym.module_name.empty()) {
				logger.error(
					c->line, c->col,
					"Method '" + method_name + "' of struct '" + struct_name +
					"' is private and cannot be accessed from outside"
				);
			}

			// Check self
			if (!method_sym.param_types.empty() && method_sym.param_names[0] == "self") {
				if (
					const auto &self_expected = method_sym.param_types[0];
					self_expected->is_pointer() && self_expected->is_mut_pointer
				) {
					if (obj_type->is_pointer() && !obj_type->is_mut_pointer)
						logger.error(
							c->line, c->col,
							"Cannot call 'var self' method on read-only pointer '*" + struct_name + "'"
						);
				}
			}

			if (
				size_t expected_args = method_sym.param_types.empty() ? 0 : method_sym.param_types.size() - 1;
				c->args.size() != expected_args
			) {
				logger.error(
					c->line, c->col, "Method '" + method_name + "' expects " +
					                 std::to_string(expected_args) + " arguments, but got " +
					                 std::to_string(c->args.size())
				);
				return method_sym.return_type;
			}

			for (size_t i = 0; i < c->args.size(); ++i) {
				auto arg_type = analyze_expr(c->args[i]);
				const auto &param_type = method_sym.param_types[i + 1];
				if (param_type->is_integer() && arg_type->is_integer() &&
				    isa<LiteralExpr>(c->args[i]) &&
				    as<LiteralExpr>(c->args[i])->literal_kind == LiteralKind::INT) {
					arg_type = param_type;
					expr_types[c->args[i]] = param_type;
				}
				if (!param_type->can_assign_from(arg_type))
					logger.error(
						c->line, c->col, "Argument " + std::to_string(i + 1) + " of method '" +
						                 method_name + "' type mismatch: expected '" +
						                 param_type->to_string() + "', got '" + arg_type->to_string() + "'"
					);
			}

			return method_sym.return_type;
		}

		logger.error(c->line, c->col, "Invalid call expression");
		return make_error();
	}

	// 8. Member access: object.field
	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);

		// Check if this MemberExpr forms a qualified symbol path to a constant (e.g. b.PI)
		if (auto path = get_symbol_path(expr, this)) {
			std::string resolved_const = resolve_const_name(*path, m->line, m->col);
			if (!resolved_const.empty()) {
				resolved_symbols[expr] = resolved_const;
				return constants.at(resolved_const).type;
			}
		}

		// Check if object is an Enum Identifier or qualified enum (e.g. Color.RED or b.Color.RED)
		if (auto obj_path = get_symbol_path(m->object, this)) {
			std::string resolved_enum = resolve_enum_name(*obj_path, m->line, m->col);
			if (!resolved_enum.empty()) {
				const auto &enum_sym = enums.at(resolved_enum);
				const auto member_name = std::string(m->member);
				if (
					auto it_m = enum_sym.member_values.find(member_name);
					it_m == enum_sym.member_values.end()
				) {
					logger.error(
						m->line, m->col,
						"Enum '" + *obj_path + "' has no member named '" + member_name + "'"
					);
					return make_error();
				}
				resolved_symbols[expr] = resolved_enum + "." + member_name;
				return make_enum(resolved_enum, enum_sym.underlying_type);
			}
		}

		auto obj_type = analyze_expr(m->object);
		if (obj_type->is_error()) return make_error();

		// Check .value property on Enum variable or expression (e.g. status.value)
		if (obj_type->is_enum()) {
			if (m->member == "value") {
				return obj_type->underlying_type
					       ? obj_type->underlying_type
					       : make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Enum type only supports property '.value'");
			return make_error();
		}

		// Check .len property on Array variable or expression (e.g. arr.len)
		if (obj_type->is_array()) {
			if (m->member == "len") {
				return make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Array type only supports property '.len'");
			return make_error();
		}

		// Check str properties (.len, .data)
		if (obj_type->is_str()) {
			if (m->member == "len") return make_primitive(SemaType::USZ);
			if (m->member == "data") return make_pointer(make_primitive(SemaType::U8));
			logger.error(m->line, m->col, "str type only supports properties '.len' and '.data'");
			return make_error();
		}

		std::string struct_name;
		if (obj_type->is_struct()) {
			struct_name = obj_type->struct_name;
		} else if (obj_type->is_pointer() && obj_type->pointee && obj_type->pointee->is_struct()) {
			struct_name = obj_type->pointee->struct_name;
		} else {
			logger.error(
				m->line, m->col, "Member access '.' is only supported on struct, struct pointer, enum, or array types"
			);
			return make_error();
		}

		auto it = structs.find(struct_name);
		if (it == structs.end()) {
			logger.error(m->line, m->col, "Struct definition '" + struct_name + "' not found");
			return make_error();
		}

		const auto field_name = std::string(m->member);
		if (
			auto it_f = it->second.field_types.find(m->member);
			it_f != it->second.field_types.end()
		)
			return it_f->second;

		if (
			auto it_m = it->second.methods.find(m->member);
			it_m != it->second.methods.end()
		)
			return it_m->second.return_type;

		logger.error(
			m->line, m->col,
			"Struct '" + struct_name + "' has no field or method named '" + field_name + "'"
		);
		return make_error();
	}

	// 9. Array/pointer indexing: target[index]
	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		auto target_type = analyze_expr(idx->target);
		auto index_type = analyze_expr(idx->index);

		if (target_type->is_error() || index_type->is_error()) return make_error();

		if (!index_type->is_integer()) {
			logger.error(idx->line, idx->col, "Index in '[]' must be an integer");
			return make_error();
		}

		if (target_type->is_array())
			return target_type->element_type ? target_type->element_type : make_error();

		if (target_type->is_pointer() && target_type->pointee) return target_type->pointee;

		// str[index] → char
		if (target_type->is_str()) return make_primitive(SemaType::CHAR);

		logger.error(idx->line, idx->col, "Index operator '[]' is only applicable to array, pointer, or str types");
		return make_error();
	}

	// 10. Parenthesized expression: (expr)
	if (isa<GroupExpr>(expr)) return analyze_expr(as<GroupExpr>(expr)->expr);

	// 11. If-expression: if (cond) then_expr else else_expr
	if (isa<IfExpr>(expr)) return analyze_if_expr(as<IfExpr>(expr));

	// 12. When-expression: when (cond) { ... }
	if (isa<WhenExpr>(expr)) return analyze_when_expr(as<WhenExpr>(expr));

	return make_error();
}

Semantic Analyzer::analyze_expr(const Expr *expr) {
	if (!expr) return make_error();
	auto ty = compute_expr_type(expr);
	expr_types[expr] = ty;
	return ty;
}

Semantic Analyzer::get_expr_type(const Expr *expr) {
	if (!expr) return make_error();
	auto it = expr_types.find(expr);
	if (it != expr_types.end()) return it->second;
	return make_error();
}

Semantic Analyzer::analyze_if_expr(const IfExpr *expr) {
	if (!expr) return make_error();

	auto cond_type = analyze_expr(expr->condition);
	if (!cond_type->is_bool() && !cond_type->is_error()) {
		logger.error(
			expr->condition->line, expr->condition->col,
			"Condition of 'if' expression must be of type 'bool', got '" + cond_type->to_string() + "'"
		);
	}

	auto then_type = analyze_expr(expr->then_branch);
	auto else_type = analyze_expr(expr->else_branch);

	if (then_type->is_error() || else_type->is_error()) return make_error();

	// Int literal contextual typing:
	if (then_type->is_integer() && else_type->is_integer() && then_type != else_type) {
		if (isa<LiteralExpr>(expr->then_branch) && as<LiteralExpr>(expr->then_branch)->literal_kind ==
		    LiteralKind::INT) {
			then_type = else_type;
			expr_types[expr->then_branch] = else_type;
		} else if (isa<LiteralExpr>(expr->else_branch) && as<LiteralExpr>(expr->else_branch)->literal_kind ==
		           LiteralKind::INT) {
			else_type = then_type;
			expr_types[expr->else_branch] = then_type;
		}
	}

	// Pointer / null compatibility
	if (then_type->is_pointer() && else_type->is_null()) return then_type;
	if (then_type->is_null() && else_type->is_pointer()) return else_type;

	if (then_type != else_type) {
		logger.error(
			expr->line, expr->col,
			"Branches of 'if' expression must have the same type, got '" +
			then_type->to_string() + "' and '" + else_type->to_string() + "'"
		);
		return make_error();
	}

	return then_type;
}

Semantic Analyzer::analyze_when_expr(const WhenExpr *expr) {
	if (!expr) return make_error();

	Semantic cond_type = nullptr;
	if (expr->condition) {
		cond_type = analyze_expr(expr->condition);
	}

	bool has_else = false;
	for (const auto &arm: expr->arms) {
		if (arm.is_else) {
			has_else = true;
		} else {
			for (const auto *pat: arm.patterns) {
				auto pat_type = analyze_expr(pat);
				if (cond_type) {
					if (cond_type->is_integer() && pat_type->is_integer() &&
					    isa<LiteralExpr>(pat) && as<LiteralExpr>(pat)->literal_kind == LiteralKind::INT) {
						pat_type = cond_type;
						expr_types[pat] = cond_type;
					}
					if (cond_type->is_enum() && pat_type->is_enum()) {
						if (cond_type != pat_type) {
							logger.error(
								pat->line, pat->col,
								"Pattern enum '" + pat_type->to_string() + "' does not match when condition enum '" +
								cond_type->to_string() + "'"
							);
						}
					} else if (!cond_type->can_assign_from(pat_type) && !pat_type->can_assign_from(cond_type)) {
						logger.error(
							pat->line, pat->col,
							"Pattern type '" + pat_type->to_string() + "' is incompatible with when condition type '" +
							cond_type->to_string() + "'"
						);
					}
				} else {
					if (!pat_type->is_bool() && !pat_type->is_error()) {
						logger.error(
							pat->line, pat->col,
							"When condition pattern must be of type 'bool', got '" + pat_type->to_string() + "'"
						);
					}
				}
			}
		}
	}

	if (!has_else) {
		logger.error(expr->line, expr->col, "'when' expression must be exhaustive and have an 'else' arm");
	}

	// Unify arm body types
	Semantic expected_type = nullptr;
	for (size_t i = 0; i < expr->arms.size(); ++i) {
		const auto &arm = expr->arms[i];
		if (!arm.body) continue;
		auto ty = analyze_expr(arm.body);
		if (ty->is_error()) continue;
		if (!expected_type || (expected_type->is_null() && ty->is_pointer())) {
			expected_type = ty;
		}
	}

	if (!expected_type) return make_error();

	// Check if expected_type is integer, see if any arm has a non-default int type
	if (expected_type->is_integer()) {
		for (size_t i = 0; i < expr->arms.size(); ++i) {
			const auto &arm = expr->arms[i];
			if (!arm.body) continue;
			auto ty = get_expr_type(arm.body);
			if (ty->is_integer() && !isa<LiteralExpr>(arm.body)) {
				expected_type = ty;
				break;
			}
		}
	}

	for (size_t i = 0; i < expr->arms.size(); ++i) {
		const auto &arm = expr->arms[i];
		if (!arm.body) continue;
		auto ty = get_expr_type(arm.body);
		if (ty->is_error()) continue;

		if (expected_type->is_integer() && ty->is_integer() &&
		    isa<LiteralExpr>(arm.body) && as<LiteralExpr>(arm.body)->literal_kind == LiteralKind::INT) {
			ty = expected_type;
			expr_types[arm.body] = expected_type;
		}

		if (expected_type->is_pointer() && ty->is_null()) {
			continue;
		}

		if (ty != expected_type) {
			logger.error(
				arm.body->line, arm.body->col,
				"When arm expression type '" + ty->to_string() +
				"' does not match expected when expression type '" + expected_type->to_string() + "'"
			);
		}
	}

	return expected_type;
}
