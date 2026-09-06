module;

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

Semantic Analyzer::compute_expr_type(const Expr *expr) {
	if (!expr) return Semantic::make_error();

	// 1. Literal
	if (isa<LiteralExpr>(expr)) {
		switch (const auto *lit = as<LiteralExpr>(expr); lit->literal_kind) {
			case LiteralKind::INT: return Semantic::make_primitive(SemaType::I32); // default int is i32
			case LiteralKind::BOOL: return Semantic::make_primitive(SemaType::BOOL);
			case LiteralKind::CHAR: return Semantic::make_primitive(SemaType::CHAR);
			case LiteralKind::STRING: return Semantic::make_pointer(Semantic::make_primitive(SemaType::CHAR));
			// *char
			case LiteralKind::NULL_VAL: return Semantic::make_null();
			default: return Semantic::make_error();
		}
	}

	// Array literal: [expr1, expr2, ...]
	if (isa<ArrayLiteralExpr>(expr)) {
		const auto *arr_lit = as<ArrayLiteralExpr>(expr);
		if (arr_lit->elements.empty()) {
			logger.error(arr_lit->line, arr_lit->col, "Array literal cannot be empty");
			return Semantic::make_error();
		}

		auto first_elem_type = analyze_expr(arr_lit->elements[0].get());
		for (size_t i = 1; i < arr_lit->elements.size(); ++i) {
			if (
				auto elem_type = analyze_expr(arr_lit->elements[i].get());
				!first_elem_type.equals(elem_type)
			) {
				logger.error(
					arr_lit->elements[i]->line, arr_lit->elements[i]->col,
					"Array elements must have the same type: expected '" +
					first_elem_type.to_string() + "', got '" + elem_type.to_string() + "'"
				);
				return Semantic::make_error();
			}
		}

		return Semantic::make_array(first_elem_type, arr_lit->elements.size());
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
		return Semantic::make_error();
	}

	// 3. Assignment: target = value
	if (isa<AssignExpr>(expr)) {
		const auto *a = as<AssignExpr>(expr);

		// Check lvalue and immutability
		Semantic target_type = Semantic::make_error();
		if (isa<IdentifierExpr>(a->target.get())) {
			const auto *id = as<IdentifierExpr>(a->target.get());
			auto *var = lookup_variable(id->name);
			if (!var) {
				logger.error(id->line, id->col, "Variable '" + std::string(id->name) + "' is not declared");
				return Semantic::make_error();
			}
			if (!var->is_mut) {
				logger.error(
					a->line, a->col, "Cannot reassign immutable variable '" + std::string(id->name) +
					                 "' declared with 'val'"
				);
				return Semantic::make_error();
			}
		} else if (isa<MemberExpr>(a->target.get())) {
			const auto *m = as<MemberExpr>(a->target.get());
			auto obj_type = analyze_expr(m->object.get());
			if (obj_type.is_enum() && m->member == "value") {
				logger.error(a->line, a->col, "Cannot assign to read-only property '.value' of enum");
				return Semantic::make_error();
			}
			if (obj_type.is_array() && m->member == "len") {
				logger.error(a->line, a->col, "Cannot assign to read-only property '.len' of array");
				return Semantic::make_error();
			}
			if (obj_type.is_pointer() && !obj_type.is_mut_pointer) {
				logger.error(a->line, a->col, "Cannot modify field through read-only pointer '*'");
				return Semantic::make_error();
			}
			target_type = analyze_expr(a->target.get());
		} else if (isa<IndexExpr>(a->target.get())) {
			target_type = analyze_expr(a->target.get());
		} else if (isa<UnaryExpr>(a->target.get()) && as<UnaryExpr>(a->target.get())->op == TokenType::STAR) {
			target_type = analyze_expr(a->target.get()); // *ptr = value
		} else {
			logger.error(a->line, a->col, "Left-hand side of assignment is not a valid lvalue");
			return Semantic::make_error();
		}

		auto val_type = analyze_expr(a->value.get());
		if (target_type.is_integer() && val_type.is_integer() &&
		    isa<LiteralExpr>(a->value.get()) &&
		    as<LiteralExpr>(a->value.get())->literal_kind == LiteralKind::INT) {
			val_type = target_type;
			expr_types[a->value.get()] = target_type;
		}

		if (!target_type.can_assign_from(val_type))
			logger.error(
				a->line, a->col, "Cannot assign value of type '" + val_type.to_string() +
				                 "' to target of type '" + target_type.to_string() + "'"
			);

		return target_type;
	}

	// 4. Binary expression: left op right
	if (isa<BinaryExpr>(expr)) {
		const auto *b = as<BinaryExpr>(expr);
		auto left_type = analyze_expr(b->left.get());
		auto right_type = analyze_expr(b->right.get());

		if (left_type.is_error() || right_type.is_error()) return Semantic::make_error();

		switch (b->op) {
			// Arithmetic operators (+, -, *, /, %)
			case TokenType::PLUS:
			case TokenType::MINUS:
			case TokenType::STAR:
			case TokenType::SLASH:
			case TokenType::PERCENT: {
				if (!left_type.is_integer() || !right_type.is_integer()) {
					logger.error(b->line, b->col, "Arithmetic operators are only applicable to integer types");
					return Semantic::make_error();
				}
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Type mismatch in arithmetic operation: '" +
						                 left_type.to_string() + "' and '" + right_type.to_string() +
						                 "'. Explicit cast with 'as' is required."
					);
					return Semantic::make_error();
				}
				return left_type;
			}

			// Comparison operators (<, <=, >, >=)
			case TokenType::LESS:
			case TokenType::LESS_EQUAL:
			case TokenType::GREATER:
			case TokenType::GREATER_EQUAL: {
				if (!left_type.is_integer() || !right_type.is_integer()) {
					logger.error(b->line, b->col, "Comparison operators are only applicable to integer types");
					return Semantic::make_error();
				}
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Type mismatch in comparison: '" +
						                 left_type.to_string() + "' and '" + right_type.to_string() + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			// Equality operators (==, !=)
			case TokenType::EQUAL_EQUAL:
			case TokenType::BANG_EQUAL: {
				if (left_type.is_pointer() && right_type.is_null()) return Semantic::make_primitive(SemaType::BOOL);
				if (left_type.is_null() && right_type.is_pointer()) return Semantic::make_primitive(SemaType::BOOL);
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Cannot compare different types: '" +
						                 left_type.to_string() + "' and '" + right_type.to_string() + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			// Logical operators (&&, ||)
			case TokenType::AND_AND:
			case TokenType::OR_OR: {
				if (!left_type.is_bool() || !right_type.is_bool()) {
					logger.error(b->line, b->col, "Logical operators '&&'/'||' are only applicable to boolean (bool) type");
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			default:
				return Semantic::make_error();
		}
	}

	// 5. Unary operators (-x, !x, *ptr)
	if (isa<UnaryExpr>(expr)) {
		const auto *u = as<UnaryExpr>(expr);
		auto operand_type = analyze_expr(u->operand.get());
		if (operand_type.is_error()) return Semantic::make_error();

		switch (u->op) {
			case TokenType::MINUS:
				if (!operand_type.is_signed_integer()) {
					logger.error(u->line, u->col, "Unary '-' operator is only applicable to signed integer types");
					return Semantic::make_error();
				}
				return operand_type;

			case TokenType::BANG:
				if (!operand_type.is_bool()) {
					logger.error(u->line, u->col, "Logical NOT '!' operator is only applicable to boolean (bool) type");
					return Semantic::make_error();
				}
				return operand_type;

			case TokenType::STAR: // Dereference: *ptr
				if (!operand_type.is_pointer() || !operand_type.pointee) {
					logger.error(u->line, u->col, "Dereference operator '*' can only be applied to pointer types");
					return Semantic::make_error();
				}
				return *operand_type.pointee;

			default:
				return Semantic::make_error();
		}
	}

	// 6. Cast expression: expr as TargetType
	if (isa<CastExpr>(expr)) {
		const auto *c = as<CastExpr>(expr);
		auto src_type = analyze_expr(c->expr.get());
		auto target_type = resolve_type(c->target_type.get());

		if (src_type.is_error() || target_type.is_error()) return Semantic::make_error();

		// Check validity of type cast:
		// - Integer -> Integer
		// - Pointer -> Pointer
		// - Integer (isz/usz) -> Pointer or Pointer -> Integer
		// - Char -> Integer or Integer -> Char
		const bool is_int_to_int = src_type.is_integer() && target_type.is_integer();
		const bool is_ptr_to_ptr = src_type.is_pointer() && target_type.is_pointer();
		const bool is_int_ptr_mix = (src_type.is_integer() && target_type.is_pointer()) ||
		                            (src_type.is_pointer() && target_type.is_integer());
		const bool is_char_int_mix = (src_type.is_char() && target_type.is_integer()) ||
		                             (src_type.is_integer() && target_type.is_char());
		const bool is_enum_int_mix = (src_type.is_enum() && target_type.is_integer()) ||
		                             (src_type.is_integer() && target_type.is_enum()) ||
		                             (src_type.is_enum() && target_type.is_enum() && src_type.equals(target_type));
		const bool is_array_to_ptr = src_type.is_array() && target_type.is_pointer() &&
		                             src_type.element_type && target_type.pointee &&
		                             src_type.element_type->equals(*target_type.pointee);

		if (!is_int_to_int && !is_ptr_to_ptr && !is_int_ptr_mix && !is_char_int_mix && !is_enum_int_mix && !
		    is_array_to_ptr) {
			logger.error(
				c->line, c->col, "Cannot cast from '" + src_type.to_string() +
				                 "' to '" + target_type.to_string() + "'"
			);
			return Semantic::make_error();
		}

		return target_type;
	}

	// 7. Function call, struct instantiation or method call: callee(args...)
	if (isa<CallExpr>(expr)) {
		const auto *c = as<CallExpr>(expr);

		// 7a. Struct instantiation or direct function call: Name(args...)
		if (isa<IdentifierExpr>(c->callee.get())) {
			const auto raw_callee_name = std::string(as<IdentifierExpr>(c->callee.get())->name);

			// Struct instantiation: Point(10, 20)
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
					return Semantic::make_struct(resolved_st);
				}

				for (size_t i = 0; i < c->args.size(); ++i) {
					auto arg_type = analyze_expr(c->args[i].get());
					const auto &field_name = st_sym.field_order[i];
					if (
						const auto &expected_type = st_sym.field_types.at(field_name);
						!expected_type.can_assign_from(arg_type)
					)
						logger.error(
							c->line, c->col, "Field '" + field_name + "' of struct '" +
							                 raw_callee_name + "' type mismatch: expected '" +
							                 expected_type.to_string() + "', got '" + arg_type.to_string() +
							                 "'"
						);
				}
				return Semantic::make_struct(resolved_st);
			}

			// Regular function call
			std::string resolved_fn = resolve_function_name(raw_callee_name, c->line, c->col);
			if (resolved_fn.empty()) {
				logger.error(c->line, c->col, "Function '" + raw_callee_name + "' is not declared");
				return Semantic::make_error();
			}

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
				auto arg_type = analyze_expr(c->args[i].get());
				if (fn_sym.param_types[i].is_integer() && arg_type.is_integer() &&
				    isa<LiteralExpr>(c->args[i].get()) &&
				    as<LiteralExpr>(c->args[i].get())->literal_kind == LiteralKind::INT) {
					arg_type = fn_sym.param_types[i];
					expr_types[c->args[i].get()] = fn_sym.param_types[i];
				}
				if (!fn_sym.param_types[i].can_assign_from(arg_type)) {
					logger.error(
						c->line, c->col, "Argument " + std::to_string(i + 1) + " of function '" +
						                 raw_callee_name + "' type mismatch: expected '" +
						                 fn_sym.param_types[i].to_string() + "', got '" + arg_type.to_string()
						                 + "'"
					);
				}
			}

			return fn_sym.return_type;
		}

		// 7b. Method call: object.method(args...)
		if (isa<MemberExpr>(c->callee.get())) {
			const auto *m = as<MemberExpr>(c->callee.get());
			auto obj_type = analyze_expr(m->object.get());
			if (obj_type.is_error()) return Semantic::make_error();

			std::string struct_name;
			if (obj_type.is_struct()) {
				struct_name = obj_type.struct_name;
			} else if (obj_type.is_pointer() && obj_type.pointee && obj_type.pointee->is_struct()) {
				struct_name = obj_type.pointee->struct_name;
			} else {
				logger.error(c->line, c->col, "Methods can only be called on structs or struct pointers");
				return Semantic::make_error();
			}

			auto it_st = structs.find(struct_name);
			if (it_st == structs.end()) {
				logger.error(c->line, c->col, "Struct definition '" + struct_name + "' not found");
				return Semantic::make_error();
			}

			const auto method_name = std::string(m->member);
			auto it_m = it_st->second.methods.find(method_name);
			if (it_m == it_st->second.methods.end()) {
				logger.error(
					c->line, c->col, "Struct '" + struct_name + "' has no method named '" + method_name + "'"
				);
				return Semantic::make_error();
			}

			const auto &method_sym = it_m->second;
			if (!method_sym.is_pub && method_sym.module_name != current_module && !method_sym.module_name.empty()) {
				logger.error(c->line, c->col, "Method '" + method_name + "' of struct '" + struct_name + "' is private and cannot be accessed from outside");
			}

			// Check self
			if (!method_sym.param_types.empty() && method_sym.param_names[0] == "self") {
				if (
					const auto &self_expected = method_sym.param_types[0];
					self_expected.is_pointer() && self_expected.is_mut_pointer
				) {
					if (obj_type.is_pointer() && !obj_type.is_mut_pointer)
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
				auto arg_type = analyze_expr(c->args[i].get());
				const auto &param_type = method_sym.param_types[i + 1];
				if (param_type.is_integer() && arg_type.is_integer() &&
				    isa<LiteralExpr>(c->args[i].get()) &&
				    as<LiteralExpr>(c->args[i].get())->literal_kind == LiteralKind::INT) {
					arg_type = param_type;
					expr_types[c->args[i].get()] = param_type;
				}
				if (!param_type.can_assign_from(arg_type))
					logger.error(
						c->line, c->col, "Argument " + std::to_string(i + 1) + " of method '" +
						                 method_name + "' type mismatch: expected '" +
						                 param_type.to_string() + "', got '" + arg_type.to_string() + "'"
					);
			}

			return method_sym.return_type;
		}

		logger.error(c->line, c->col, "Invalid call expression");
		return Semantic::make_error();
	}

	// 8. Member access: object.field
	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);

		// Check if object is an Enum Identifier (e.g. Status.OK)
		if (isa<IdentifierExpr>(m->object.get())) {
			const auto id_name = std::string(as<IdentifierExpr>(m->object.get())->name);
			std::string resolved_enum = resolve_enum_name(id_name, m->line, m->col);
			if (!resolved_enum.empty()) {
				const auto &enum_sym = enums.at(resolved_enum);
				const auto member_name = std::string(m->member);
				if (
					auto it_m = enum_sym.member_values.find(member_name);
					it_m == enum_sym.member_values.end()
				) {
					logger.error(
						m->line, m->col,
						"Enum '" + id_name + "' has no member named '" + member_name + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_enum(resolved_enum, enum_sym.underlying_type);
			}
		}

		auto obj_type = analyze_expr(m->object.get());
		if (obj_type.is_error()) return Semantic::make_error();

		// Check .value property on Enum variable or expression (e.g. status.value)
		if (obj_type.is_enum()) {
			if (m->member == "value") {
				return obj_type.underlying_type
					       ? *obj_type.underlying_type
					       : Semantic::make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Enum type only supports property '.value'");
			return Semantic::make_error();
		}

		// Check .len property on Array variable or expression (e.g. arr.len)
		if (obj_type.is_array()) {
			if (m->member == "len") {
				return Semantic::make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Array type only supports property '.len'");
			return Semantic::make_error();
		}

		std::string struct_name;
		if (obj_type.is_struct()) {
			struct_name = obj_type.struct_name;
		} else if (obj_type.is_pointer() && obj_type.pointee && obj_type.pointee->is_struct()) {
			struct_name = obj_type.pointee->struct_name;
		} else {
			logger.error(
				m->line, m->col, "Member access '.' is only supported on struct, struct pointer, enum, or array types"
			);
			return Semantic::make_error();
		}

		auto it = structs.find(struct_name);
		if (it == structs.end()) {
			logger.error(m->line, m->col, "Struct definition '" + struct_name + "' not found");
			return Semantic::make_error();
		}

		const auto field_name = std::string(m->member);
		if (
			auto it_f = it->second.field_types.find(field_name);
			it_f != it->second.field_types.end()
		)
			return it_f->second;

		if (
			auto it_m = it->second.methods.find(field_name);
			it_m != it->second.methods.end()
		)
			return it_m->second.return_type;

		logger.error(
			m->line, m->col,
			"Struct '" + struct_name + "' has no field or method named '" + field_name + "'"
		);
		return Semantic::make_error();
	}

	// 9. Array/pointer indexing: target[index]
	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		auto target_type = analyze_expr(idx->target.get());
		auto index_type = analyze_expr(idx->index.get());

		if (target_type.is_error() || index_type.is_error()) return Semantic::make_error();

		if (!index_type.is_integer()) {
			logger.error(idx->line, idx->col, "Index in '[]' must be an integer");
			return Semantic::make_error();
		}

		if (target_type.is_array())
			return target_type.element_type ? *target_type.element_type : Semantic::make_error();

		if (target_type.is_pointer() && target_type.pointee) return *target_type.pointee;

		logger.error(idx->line, idx->col, "Index operator '[]' is only applicable to array or pointer types");
		return Semantic::make_error();
	}

	// 10. Parenthesized expression: (expr)
	if (isa<GroupExpr>(expr)) return analyze_expr(as<GroupExpr>(expr)->expr.get());

	return Semantic::make_error();
}

Semantic Analyzer::analyze_expr(const Expr *expr) {
	if (!expr) return Semantic::make_error();
	auto ty = compute_expr_type(expr);
	expr_types[expr] = ty;
	return ty;
}

Semantic Analyzer::get_expr_type(const Expr *expr) const {
	if (!expr) return Semantic::make_error();
	auto it = expr_types.find(expr);
	if (it != expr_types.end()) return it->second;
	return Semantic::make_error();
}
