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
			case LiteralKind::INT: return Semantic::make_primitive(SemaType::I32); // mặc định int là i32
			case LiteralKind::BOOL: return Semantic::make_primitive(SemaType::BOOL);
			case LiteralKind::CHAR: return Semantic::make_primitive(SemaType::CHAR);
			case LiteralKind::STRING: return Semantic::make_pointer(Semantic::make_primitive(SemaType::CHAR));
			// *char
			case LiteralKind::NULL_VAL: return Semantic::make_null();
			default: return Semantic::make_error();
		}
	}

	// Literal mảng: [expr1, expr2, ...]
	if (isa<ArrayLiteralExpr>(expr)) {
		const auto *arr_lit = as<ArrayLiteralExpr>(expr);
		if (arr_lit->elements.empty()) {
			logger.error(arr_lit->line, arr_lit->col, "Literal mảng không được để rỗng");
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
					"Các phần tử trong mảng phải có cùng kiểu dữ liệu: mong đợi '" +
					first_elem_type.to_string() + "', nhận được '" + elem_type.to_string() + "'"
				);
				return Semantic::make_error();
			}
		}

		return Semantic::make_array(first_elem_type, arr_lit->elements.size());
	}

	// 2. Biến / Định danh
	if (isa<IdentifierExpr>(expr)) {
		const auto *id = as<IdentifierExpr>(expr);
		const auto name = id->name;

		// Tra cứu biến cục bộ / tham số
		if (auto *var = lookup_variable(name)) return var->type;

		// Tra cứu hằng số
		std::string resolved_c = resolve_const_name(name, id->line, id->col);
		if (!resolved_c.empty()) {
			resolved_symbols[id] = resolved_c;
			return constants.at(resolved_c).type;
		}

		logger.error(id->line, id->col, "Biến hoặc định danh '" + std::string(name) + "' chưa được khai báo");
		return Semantic::make_error();
	}

	// 3. Phép gán: target = value
	if (isa<AssignExpr>(expr)) {
		const auto *a = as<AssignExpr>(expr);

		// Kiểm tra lvalue và tính bất biến (Quy tắc 4)
		Semantic target_type = Semantic::make_error();
		if (isa<IdentifierExpr>(a->target.get())) {
			const auto *id = as<IdentifierExpr>(a->target.get());
			auto *var = lookup_variable(id->name);
			if (!var) {
				logger.error(id->line, id->col, "Biến '" + std::string(id->name) + "' chưa được khai báo");
				return Semantic::make_error();
			}
			if (!var->is_mut) {
				logger.error(
					a->line, a->col, "Không thể gán lại biến bất biến '" + std::string(id->name) +
					                 "' được khai báo bằng 'val'"
				);
				return Semantic::make_error();
			}
		} else if (isa<MemberExpr>(a->target.get())) {
			const auto *m = as<MemberExpr>(a->target.get());
			auto obj_type = analyze_expr(m->object.get());
			if (obj_type.is_enum() && m->member == "value") {
				logger.error(a->line, a->col, "Không thể gán giá trị cho thuộc tính chỉ đọc '.value' của enum");
				return Semantic::make_error();
			}
			if (obj_type.is_array() && m->member == "len") {
				logger.error(a->line, a->col, "Không thể gán giá trị cho thuộc tính chỉ đọc '.len' của mảng");
				return Semantic::make_error();
			}
			if (obj_type.is_pointer() && !obj_type.is_mut_pointer) {
				logger.error(a->line, a->col, "Không thể sửa trường thông qua con trỏ chỉ đọc '*'");
				return Semantic::make_error();
			}
			target_type = analyze_expr(a->target.get());
		} else if (isa<IndexExpr>(a->target.get())) {
			target_type = analyze_expr(a->target.get());
		} else if (isa<UnaryExpr>(a->target.get()) && as<UnaryExpr>(a->target.get())->op == TokenType::STAR) {
			target_type = analyze_expr(a->target.get()); // *ptr = value
		} else {
			logger.error(a->line, a->col, "Vế trái của phép gán không phải là lvalue hợp lệ");
			return Semantic::make_error();
		}

		if (
			auto val_type = analyze_expr(a->value.get());
			!target_type.can_assign_from(val_type)
		)
			logger.error(
				a->line, a->col, "Không thể gán giá trị kiểu '" + val_type.to_string() +
				                 "' cho đích kiểu '" + target_type.to_string() + "'"
			);

		return target_type;
	}

	// 4. Biểu thức Nhị phân: left op right
	if (isa<BinaryExpr>(expr)) {
		const auto *b = as<BinaryExpr>(expr);
		auto left_type = analyze_expr(b->left.get());
		auto right_type = analyze_expr(b->right.get());

		if (left_type.is_error() || right_type.is_error()) return Semantic::make_error();

		switch (b->op) {
			// Toán tử số học (+, -, *, /, %)
			case TokenType::PLUS:
			case TokenType::MINUS:
			case TokenType::STAR:
			case TokenType::SLASH:
			case TokenType::PERCENT: {
				if (!left_type.is_integer() || !right_type.is_integer()) {
					logger.error(b->line, b->col, "Toán tử số học chỉ áp dụng cho kiểu số nguyên");
					return Semantic::make_error();
				}
				// Quy tắc 3: Bắt buộc ép kiểu tường minh, không implicit widening
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Không khớp kiểu trong phép toán số học: '" +
						                 left_type.to_string() + "' và '" + right_type.to_string() +
						                 "'. Cần dùng 'as' để ép kiểu tường minh."
					);
					return Semantic::make_error();
				}
				return left_type;
			}

			// So sánh thứ tự (<, <=, >, >=)
			case TokenType::LESS:
			case TokenType::LESS_EQUAL:
			case TokenType::GREATER:
			case TokenType::GREATER_EQUAL: {
				if (!left_type.is_integer() || !right_type.is_integer()) {
					logger.error(b->line, b->col, "Toán tử so sánh thứ tự chỉ áp dụng cho kiểu số nguyên");
					return Semantic::make_error();
				}
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Không khớp kiểu trong phép so sánh: '" +
						                 left_type.to_string() + "' và '" + right_type.to_string() + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			// So sánh bằng (==, !=)
			case TokenType::EQUAL_EQUAL:
			case TokenType::BANG_EQUAL: {
				if (left_type.is_pointer() && right_type.is_null()) return Semantic::make_primitive(SemaType::BOOL);
				if (left_type.is_null() && right_type.is_pointer()) return Semantic::make_primitive(SemaType::BOOL);
				if (!left_type.equals(right_type)) {
					logger.error(
						b->line, b->col, "Không thể so sánh giữa 2 kiểu khác nhau: '" +
						                 left_type.to_string() + "' và '" + right_type.to_string() + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			// Logic (&&, ||)
			case TokenType::AND_AND:
			case TokenType::OR_OR: {
				if (!left_type.is_bool() || !right_type.is_bool()) {
					logger.error(b->line, b->col, "Toán tử logic '&&'/'||' chỉ áp dụng cho kiểu boolean (bool)");
					return Semantic::make_error();
				}
				return Semantic::make_primitive(SemaType::BOOL);
			}

			default:
				return Semantic::make_error();
		}
	}

	// 5. Toán tử Một ngôi (-x, !x, *ptr)
	if (isa<UnaryExpr>(expr)) {
		const auto *u = as<UnaryExpr>(expr);
		auto operand_type = analyze_expr(u->operand.get());
		if (operand_type.is_error()) return Semantic::make_error();

		switch (u->op) {
			case TokenType::MINUS:
				if (!operand_type.is_signed_integer()) {
					logger.error(u->line, u->col, "Toán tử âm '-' chỉ áp dụng cho số nguyên có dấu");
					return Semantic::make_error();
				}
				return operand_type;

			case TokenType::BANG:
				if (!operand_type.is_bool()) {
					logger.error(u->line, u->col, "Toán tử phủ định '!' chỉ áp dụng cho kiểu boolean (bool)");
					return Semantic::make_error();
				}
				return operand_type;

			case TokenType::STAR: // Giải tham chiếu: *ptr
				if (!operand_type.is_pointer() || !operand_type.pointee) {
					logger.error(u->line, u->col, "Chỉ có thể giải tham chiếu '*' trên kiểu con trỏ");
					return Semantic::make_error();
				}
				return *operand_type.pointee;

			default:
				return Semantic::make_error();
		}
	}

	// 6. Ép kiểu: expr as TargetType
	if (isa<CastExpr>(expr)) {
		const auto *c = as<CastExpr>(expr);
		auto src_type = analyze_expr(c->expr.get());
		auto target_type = resolve_type(c->target_type.get());

		if (src_type.is_error() || target_type.is_error()) return Semantic::make_error();

		// Kiểm tra tính hợp lệ của ép kiểu:
		// - Số nguyên -> Số nguyên
		// - Con trỏ -> Con trỏ
		// - Số nguyên (isz/usz) -> Con trỏ hoặc Con trỏ -> Số nguyên
		// - Char -> Số nguyên hoặc Số nguyên -> Char
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
				c->line, c->col, "Không thể ép kiểu từ '" + src_type.to_string() +
				                 "' sang '" + target_type.to_string() + "'"
			);
			return Semantic::make_error();
		}

		return target_type;
	}

	// 7. Lệnh gọi hàm, khởi tạo struct hoặc gọi phương thức: callee(args...)
	if (isa<CallExpr>(expr)) {
		const auto *c = as<CallExpr>(expr);

		// 7a. Khởi tạo struct hoặc gọi hàm trực tiếp: Name(args...)
		if (isa<IdentifierExpr>(c->callee.get())) {
			const auto raw_callee_name = std::string(as<IdentifierExpr>(c->callee.get())->name);

			// Khởi tạo struct: Point(10, 20)
			std::string resolved_st = resolve_struct_name(raw_callee_name, c->line, c->col);
			if (!resolved_st.empty()) {
				resolved_symbols[c] = resolved_st;
				const auto &st_sym = structs.at(resolved_st);
				if (c->args.size() != st_sym.field_order.size()) {
					logger.error(
						c->line, c->col, "Khởi tạo struct '" + raw_callee_name + "' mong đợi " +
						                 std::to_string(st_sym.field_order.size()) + " đối số, nhưng nhận được " +
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
							c->line, c->col, "Trường '" + field_name + "' của struct '" +
							                 raw_callee_name + "' không khớp kiểu: mong đợi '" +
							                 expected_type.to_string() + "', nhận được '" + arg_type.to_string() +
							                 "'"
						);
				}
				return Semantic::make_struct(resolved_st);
			}

			// Gọi hàm thông thường
			std::string resolved_fn = resolve_function_name(raw_callee_name, c->line, c->col);
			if (resolved_fn.empty()) {
				logger.error(c->line, c->col, "Hàm '" + raw_callee_name + "' chưa được khai báo");
				return Semantic::make_error();
			}

			resolved_symbols[c] = resolved_fn;
			const auto &fn_sym = functions.at(resolved_fn);
			if (c->args.size() != fn_sym.param_types.size()) {
				logger.error(
					c->line, c->col, "Hàm '" + raw_callee_name + "' mong đợi " +
					                 std::to_string(fn_sym.param_types.size()) + " đối số, nhưng nhận được " +
					                 std::to_string(c->args.size())
				);
				return fn_sym.return_type;
			}

			for (size_t i = 0; i < c->args.size(); ++i) {
				if (
					auto arg_type = analyze_expr(c->args[i].get());
					!fn_sym.param_types[i].can_assign_from(arg_type)
				) {
					logger.error(
						c->line, c->col, "Đối số " + std::to_string(i + 1) + " của hàm '" +
						                 raw_callee_name + "' không khớp kiểu: mong đợi '" +
						                 fn_sym.param_types[i].to_string() + "', nhận được '" + arg_type.to_string()
						                 + "'"
					);
				}
			}

			return fn_sym.return_type;
		}

		// 7b. Gọi phương thức: object.method(args...)
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
				logger.error(c->line, c->col, "Chỉ có thể gọi phương thức trên struct hoặc con trỏ struct");
				return Semantic::make_error();
			}

			auto it_st = structs.find(struct_name);
			if (it_st == structs.end()) {
				logger.error(c->line, c->col, "Không tìm thấy định nghĩa struct '" + struct_name + "'");
				return Semantic::make_error();
			}

			const auto method_name = std::string(m->member);
			auto it_m = it_st->second.methods.find(method_name);
			if (it_m == it_st->second.methods.end()) {
				logger.error(
					c->line, c->col, "Struct '" + struct_name + "' không có phương thức '" + method_name + "'"
				);
				return Semantic::make_error();
			}

			const auto &method_sym = it_m->second;
			if (!method_sym.is_pub && method_sym.module_name != current_module && !method_sym.module_name.empty()) {
				logger.error(c->line, c->col, "Phương thức '" + method_name + "' của struct '" + struct_name + "' là private và không thể truy cập từ bên ngoài");
			}

			// Kiểm tra self
			if (!method_sym.param_types.empty() && method_sym.param_names[0] == "self") {
				if (
					const auto &self_expected = method_sym.param_types[0];
					self_expected.is_pointer() && self_expected.is_mut_pointer
				) {
					if (obj_type.is_pointer() && !obj_type.is_mut_pointer)
						logger.error(
							c->line, c->col,
							"Không thể gọi phương thức 'var self' trên con trỏ chỉ đọc '*" + struct_name + "'"
						);
				}
			}

			if (
				size_t expected_args = method_sym.param_types.empty() ? 0 : method_sym.param_types.size() - 1;
				c->args.size() != expected_args
			) {
				logger.error(
					c->line, c->col, "Phương thức '" + method_name + "' mong đợi " +
					                 std::to_string(expected_args) + " đối số, nhưng nhận được " +
					                 std::to_string(c->args.size())
				);
				return method_sym.return_type;
			}

			for (size_t i = 0; i < c->args.size(); ++i) {
				auto arg_type = analyze_expr(c->args[i].get());
				if (
					const auto &param_type = method_sym.param_types[i + 1];
					!param_type.can_assign_from(arg_type)
				)
					logger.error(
						c->line, c->col, "Đối số " + std::to_string(i + 1) + " của phương thức '" +
						                 method_name + "' không khớp kiểu: mong đợi '" +
						                 param_type.to_string() + "', nhận được '" + arg_type.to_string() + "'"
					);
			}

			return method_sym.return_type;
		}

		logger.error(c->line, c->col, "Biểu thức gọi không hợp lệ");
		return Semantic::make_error();
	}

	// 8. Truy cập trường struct hoặc thành viên enum: object.field
	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);

		// Kiểm tra nếu object là Identifier của một Enum (vd: Status.OK)
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
						"Enum '" + id_name + "' không có thành viên nào tên là '" + member_name + "'"
					);
					return Semantic::make_error();
				}
				return Semantic::make_enum(resolved_enum, enum_sym.underlying_type);
			}
		}

		auto obj_type = analyze_expr(m->object.get());
		if (obj_type.is_error()) return Semantic::make_error();

		// Kiểm tra thuộc tính .value trên biến hoặc biểu thức Enum (vd: status.value)
		if (obj_type.is_enum()) {
			if (m->member == "value") {
				return obj_type.underlying_type
					       ? *obj_type.underlying_type
					       : Semantic::make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Kiểu enum chỉ hỗ trợ thuộc tính '.value'");
			return Semantic::make_error();
		}

		// Kiểm tra thuộc tính .len trên biến hoặc biểu thức Mảng (vd: arr.len)
		if (obj_type.is_array()) {
			if (m->member == "len") {
				return Semantic::make_primitive(SemaType::I32);
			}
			logger.error(m->line, m->col, "Kiểu mảng chỉ hỗ trợ thuộc tính '.len'");
			return Semantic::make_error();
		}

		std::string struct_name;
		if (obj_type.is_struct()) {
			struct_name = obj_type.struct_name;
		} else if (obj_type.is_pointer() && obj_type.pointee && obj_type.pointee->is_struct()) {
			struct_name = obj_type.pointee->struct_name;
		} else {
			logger.error(
				m->line, m->col, "Chỉ có thể truy cập trường '.' trên kiểu struct, con trỏ struct, enum hoặc mảng"
			);
			return Semantic::make_error();
		}

		auto it = structs.find(struct_name);
		if (it == structs.end()) {
			logger.error(m->line, m->col, "Không tìm thấy định nghĩa của struct '" + struct_name + "'");
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
			"Struct '" + struct_name + "' không có trường hay phương thức nào tên là '" + field_name + "'"
		);
		return Semantic::make_error();
	}

	// 9. Chỉ mục mảng/con trỏ: target[index]
	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		auto target_type = analyze_expr(idx->target.get());
		auto index_type = analyze_expr(idx->index.get());

		if (target_type.is_error() || index_type.is_error()) return Semantic::make_error();

		if (!index_type.is_integer()) {
			logger.error(idx->line, idx->col, "Chỉ mục trong '[]' phải là số nguyên");
			return Semantic::make_error();
		}

		if (target_type.is_array())
			return target_type.element_type ? *target_type.element_type : Semantic::make_error();

		if (target_type.is_pointer() && target_type.pointee) return *target_type.pointee;

		logger.error(idx->line, idx->col, "Chỉ mục '[]' chỉ áp dụng cho kiểu mảng hoặc con trỏ");
		return Semantic::make_error();
	}

	// 10. Nhóm ngoặc: (expr)
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
