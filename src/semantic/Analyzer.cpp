module;

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module semantic.analyzer;

import ast;

import token;
import logger;
import semantic;
import semantic.symbol;

export struct Analyzer {
	DiagnosticEngine& logger;

	std::unordered_map<std::string, FnSymbol> functions;
	std::unordered_map<std::string, StructSymbol> structs;
	std::unordered_map<std::string, EnumSymbol> enums;
	std::unordered_map<std::string, ConstSymbol> constants;
	std::unordered_map<const Expr*, Semantic> expr_types;

	std::vector<Scope> scopes;
	std::optional<Semantic> current_function_return_type;
	int loop_depth = 0;

	explicit Analyzer(DiagnosticEngine& log) : logger(log) {}

	// Scope helpers
	void enter_scope() {
		scopes.emplace_back();
	}

	void exit_scope() {
		if (!scopes.empty()) scopes.pop_back();
	}

	Scope& current_scope() {
		return scopes.back();
	}

	VarSymbol* lookup_variable(const std::string_view name) {
		for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
			auto found = it->variables.find(std::string(name));
			if (found != it->variables.end()) {
				return &found->second;
			}
		}
		return nullptr;
	}

	// ========================================================================
	// Ánh xạ TypeNode (AST) -> Semantic (Semantic)
	// ========================================================================

	Semantic resolve_type(const TypeNode* node) {
		if (!node) return Semantic::make_primitive(SemaType::VOID);

		if (isa<NamedType>(node)) {
			const auto* named = as<NamedType>(node);
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
			auto it = structs.find(std::string(name));
			if (it != structs.end()) {
				return Semantic::make_struct(name);
			}

			// Kiểm tra enum đã khai báo
			auto it_enum = enums.find(std::string(name));
			if (it_enum != enums.end()) {
				return Semantic::make_enum(name, it_enum->second.underlying_type);
			}

			logger.error(node->line, node->col, "Không tìm thấy kiểu dữ liệu '" + std::string(name) + "'");
			return Semantic::make_error();
		}

		if (isa<PointerType>(node)) {
			const auto* ptr = as<PointerType>(node);
			auto pointee_type = resolve_type(ptr->pointee.get());
			return Semantic::make_pointer(std::move(pointee_type));
		}

		logger.error(node->line, node->col, "Kiểu dữ liệu cú pháp không hợp lệ");
		return Semantic::make_error();
	}

	// ========================================================================
	// Pass 1: Đăng ký Khai báo Top-Level (Hoisting)
	// ========================================================================

	void pass1_register_declarations(const Program* program) {
		// 1. Đăng ký các Struct
		for (const auto& decl : program->declarations) {
			if (isa<StructDecl>(decl.get())) {
				const auto* st = as<StructDecl>(decl.get());
				const std::string name = std::string(st->name);

				if (structs.contains(name)) {
					logger.error(st->line, st->col, "Trùng lặp khai báo struct '" + name + "'");
					continue;
				}

				StructSymbol sym;
				sym.name = name;
				sym.line = st->line;
				sym.col = st->col;
				structs[name] = sym;
			}
		}

		// Sau khi có tên struct, đăng ký các trường của struct
		for (const auto& decl : program->declarations) {
			if (isa<StructDecl>(decl.get())) {
				const auto* st = as<StructDecl>(decl.get());
				auto& sym = structs[std::string(st->name)];

				for (const auto& f : st->fields) {
					std::string f_name = std::string(f.name);
					if (sym.field_types.contains(f_name)) {
						logger.error(st->line, st->col, "Trùng lặp trường '" + f_name + "' trong struct '" + sym.name + "'");
						continue;
					}
					auto f_type = resolve_type(f.type.get());
					sym.field_types[f_name] = f_type;
					sym.field_order.push_back(f_name);
				}
			}
		}

		// 2. Đăng ký các Enum
		for (const auto& decl : program->declarations) {
			if (isa<EnumDecl>(decl.get())) {
				const auto* e = as<EnumDecl>(decl.get());
				const std::string name = std::string(e->name);

				if (enums.contains(name) || structs.contains(name)) {
					logger.error(e->line, e->col, "Trùng lặp tên kiểu '" + name + "'");
					continue;
				}

				EnumSymbol sym;
				sym.name = name;
				sym.underlying_type = e->underlying_type
					? resolve_type(e->underlying_type.get())
					: Semantic::make_primitive(SemaType::I32);
				sym.line = e->line;
				sym.col = e->col;

				if (!sym.underlying_type.is_integer()) {
					logger.error(e->line, e->col, "Kiểu cơ sở của enum bắt buộc phải là số nguyên");
					sym.underlying_type = Semantic::make_primitive(SemaType::I32);
				}

				int64_t next_value = 0;
				for (const auto& m : e->members) {
					std::string m_name = std::string(m.name);
					if (sym.member_values.contains(m_name)) {
						logger.error(m.line, m.col, "Trùng lặp thành viên '" + m_name + "' trong enum '" + name + "'");
						continue;
					}

					if (m.value) {
						if (isa<LiteralExpr>(m.value.get())) {
							const auto* lit = as<LiteralExpr>(m.value.get());
							if (lit->literal_kind == LiteralKind::INT) {
								try {
									next_value = std::stoll(std::string(lit->raw_text), nullptr, 0);
								} catch (...) {
									logger.error(m.line, m.col, "Giá trị khởi tạo enum không hợp lệ");
								}
							} else {
								logger.error(m.line, m.col, "Giá trị khởi tạo enum phải là số nguyên");
							}
						} else {
							logger.error(m.line, m.col, "Hiện tại chỉ hỗ trợ khởi tạo enum bằng hằng số nguyên");
						}
					}

					sym.member_values[m_name] = next_value;
					next_value++;
				}

				enums[name] = std::move(sym);
			}
		}

		// 3. Đăng ký các Hằng số
		for (const auto& decl : program->declarations) {
			if (isa<ConstDecl>(decl.get())) {
				const auto* c = as<ConstDecl>(decl.get());
				const std::string name = std::string(c->name);

				if (constants.contains(name)) {
					logger.error(c->line, c->col, "Trùng lặp khai báo hằng số '" + name + "'");
					continue;
				}

				ConstSymbol sym;
				sym.name = name;
				sym.type = resolve_type(c->type.get());
				sym.line = c->line;
				sym.col = c->col;
				constants[name] = sym;
			}
		}

		// 3. Đăng ký Hàm (bao gồm cả khối extern)
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl.get())) {
				register_function(as<FnDecl>(decl.get()));
			} else if (isa<ExternBlock>(decl.get())) {
				const auto* ext = as<ExternBlock>(decl.get());
				for (const auto& fn : ext->declarations) {
					register_function(fn.get());
				}
			}
		}
	}

	void register_function(const FnDecl* fn) {
		const std::string name = std::string(fn->name);

		if (functions.contains(name)) {
			logger.error(fn->line, fn->col, "Trùng lặp khai báo hàm '" + name + "'");
			return;
		}

		FnSymbol sym;
		sym.name = name;
		sym.return_type = resolve_type(fn->return_type.get());
		sym.line = fn->line;
		sym.col = fn->col;

		for (const auto& p : fn->params) {
			sym.param_names.push_back(std::string(p.name));
			sym.param_types.push_back(resolve_type(p.type.get()));
		}

		functions[name] = sym;
	}

	// ========================================================================
	// Pass 2: Kiểm tra Ngữ nghĩa & Kiểu Thân Hàm (Type Checking)
	// ========================================================================

	void pass2_check_declarations(const Program* program) {
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl.get())) {
				check_function(as<FnDecl>(decl.get()));
			} else if (isa<ConstDecl>(decl.get())) {
				const auto* c = as<ConstDecl>(decl.get());
				auto val_type = analyze_expr(c->value.get());
				auto expected_type = constants[std::string(c->name)].type;
				if (!expected_type.can_assign_from(val_type)) {
					logger.error(c->line, c->col, "Giá trị khởi tạo hằng số không khớp kiểu: mong đợi '" +
					             expected_type.to_string() + "', gặp '" + val_type.to_string() + "'");
				}
			}
		}
	}

	void check_function(const FnDecl* fn) {
		if (!fn->body) return; // Hàm prototype không có thân

		const auto& sym = functions[std::string(fn->name)];
		current_function_return_type = sym.return_type;

		enter_scope(); // Scope mức hàm

		// Đăng ký tham số hàm (mặc định là val bất biến)
		for (size_t i = 0; i < sym.param_names.size(); ++i) {
			VarSymbol p_sym;
			p_sym.name = sym.param_names[i];
			p_sym.type = sym.param_types[i];
			p_sym.is_mut = false; // val
			p_sym.line = fn->line;
			p_sym.col = fn->col;
			current_scope().variables[p_sym.name] = p_sym;
		}

		// Duyệt các câu lệnh trong thân hàm
		for (const auto& stmt : fn->body->statements) {
			analyze_stmt(stmt.get());
		}

		exit_scope();
		current_function_return_type.reset();
	}

	// ========================================================================
	// Thẩm định Câu lệnh (Statements)
	// ========================================================================

	void analyze_stmt(const Stmt* stmt) {
		if (!stmt) return;

		// 1. Khai báo biến: val / var
		if (isa<VarDeclStmt>(stmt)) {
			const auto* v = as<VarDeclStmt>(stmt);
			const std::string name = std::string(v->name);

			// Quy tắc 2: Bắt buộc ghi kiểu tường minh
			if (!v->type_annotation) {
				logger.error(v->line, v->col, "Bắt buộc phải ghi kiểu dữ liệu tường minh cho biến '" + name + "'");
				return;
			}

			auto declared_type = resolve_type(v->type_annotation.get());

			// Kiểm tra giá trị khởi tạo nếu có
			if (v->initializer) {
				auto init_type = analyze_expr(v->initializer.get());
				if (!declared_type.can_assign_from(init_type)) {
					logger.error(v->line, v->col, "Không thể khởi tạo biến '" + name + "' kiểu '" +
					             declared_type.to_string() + "' bằng giá trị kiểu '" + init_type.to_string() + "'");
				}
			}

			// Kiểm tra trùng lặp trong cùng một scope
			if (current_scope().variables.contains(name)) {
				logger.error(v->line, v->col, "Biến '" + name + "' đã được khai báo trước đó trong cùng phạm vi");
				return;
			}

			VarSymbol sym;
			sym.name = name;
			sym.type = declared_type;
			sym.is_mut = v->is_mut;
			sym.line = v->line;
			sym.col = v->col;
			current_scope().variables[name] = sym;
			return;
		}

		// 2. Khối lệnh: { ... }
		if (isa<BlockStmt>(stmt)) {
			const auto* b = as<BlockStmt>(stmt);
			enter_scope();
			for (const auto& s : b->statements) {
				analyze_stmt(s.get());
			}
			exit_scope();
			return;
		}

		// 3. Câu lệnh if: if (cond) { ... } else { ... }
		if (isa<IfStmt>(stmt)) {
			const auto* i = as<IfStmt>(stmt);
			auto cond_type = analyze_expr(i->condition.get());
			if (!cond_type.is_bool() && !cond_type.is_error()) {
				logger.error(i->line, i->col, "Điều kiện if phải có kiểu 'bool', gặp kiểu '" + cond_type.to_string() + "'");
			}
			analyze_stmt(i->then_branch.get());
			if (i->else_branch) {
				analyze_stmt(i->else_branch.get());
			}
			return;
		}

		// 4. Vòng lặp while: while (cond) { ... }
		if (isa<WhileStmt>(stmt)) {
			const auto* w = as<WhileStmt>(stmt);
			auto cond_type = analyze_expr(w->condition.get());
			if (!cond_type.is_bool() && !cond_type.is_error()) {
				logger.error(w->line, w->col, "Điều kiện while phải có kiểu 'bool', gặp kiểu '" + cond_type.to_string() + "'");
			}
			loop_depth++;
			analyze_stmt(w->body.get());
			loop_depth--;
			return;
		}

		// 5. Câu lệnh return: return expr;
		if (isa<ReturnStmt>(stmt)) {
			const auto* r = as<ReturnStmt>(stmt);
			if (!current_function_return_type.has_value()) {
				logger.error(r->line, r->col, "Lệnh 'return' chỉ hợp lệ bên trong thân hàm");
				return;
			}

			const auto expected = current_function_return_type.value();
			if (r->value) {
				auto val_type = analyze_expr(r->value.get());
				if (!expected.can_assign_from(val_type)) {
					logger.error(r->line, r->col, "Kiểu giá trị trả về '" + val_type.to_string() +
					             "' không khớp với kiểu hàm mong đợi '" + expected.to_string() + "'");
				}
			} else {
				if (!expected.is_void()) {
					logger.error(r->line, r->col, "Hàm mong đợi trả về kiểu '" + expected.to_string() +
					             "', không được dùng lệnh return rỗng");
				}
			}
			return;
		}

		// 6. Break & Continue
		if (isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt)) {
			if (loop_depth <= 0) {
				logger.error(stmt->line, stmt->col, "Lệnh 'break'/'continue' chỉ được phép nằm bên trong vòng lặp while");
			}
			return;
		}

		// 7. Câu lệnh biểu thức: expr;
		if (isa<ExprStmt>(stmt)) {
			const auto* e = as<ExprStmt>(stmt);
			analyze_expr(e->expr.get());
			return;
		}
	}

	// ========================================================================
	// Thẩm định Biểu thức (Expressions) & Trả về Kiểu Ngữ nghĩa
	// ========================================================================

	Semantic compute_expr_type(const Expr* expr) {
		if (!expr) return Semantic::make_error();

		// 1. Literal
		if (isa<LiteralExpr>(expr)) {
			const auto* lit = as<LiteralExpr>(expr);
			switch (lit->literal_kind) {
				case LiteralKind::INT: return Semantic::make_primitive(SemaType::I32); // mặc định int là i32
				case LiteralKind::BOOL: return Semantic::make_primitive(SemaType::BOOL);
				case LiteralKind::CHAR: return Semantic::make_primitive(SemaType::CHAR);
				case LiteralKind::STRING: return Semantic::make_pointer(Semantic::make_primitive(SemaType::CHAR)); // *char
				case LiteralKind::NULL_VAL: return Semantic::make_null();
				default: return Semantic::make_error();
			}
		}

		// 2. Biến / Định danh
		if (isa<IdentifierExpr>(expr)) {
			const auto* id = as<IdentifierExpr>(expr);
			const auto name = id->name;

			// Tra cứu biến cục bộ / tham số
			if (auto* var = lookup_variable(name)) {
				return var->type;
			}

			// Tra cứu hằng số
			auto it_c = constants.find(std::string(name));
			if (it_c != constants.end()) {
				return it_c->second.type;
			}

			logger.error(id->line, id->col, "Biến hoặc định danh '" + std::string(name) + "' chưa được khai báo");
			return Semantic::make_error();
		}

		// 3. Phép gán: target = value
		if (isa<AssignExpr>(expr)) {
			const auto* a = as<AssignExpr>(expr);

			// Kiểm tra lvalue và tính bất biến (Quy tắc 4)
			Semantic target_type = Semantic::make_error();
			if (isa<IdentifierExpr>(a->target.get())) {
				const auto* id = as<IdentifierExpr>(a->target.get());
				auto* var = lookup_variable(id->name);
				if (!var) {
					logger.error(id->line, id->col, "Biến '" + std::string(id->name) + "' chưa được khai báo");
					return Semantic::make_error();
				}
				if (!var->is_mut) {
					logger.error(a->line, a->col, "Không thể gán lại biến bất biến '" + std::string(id->name) +
					             "' được khai báo bằng 'val'");
					return Semantic::make_error();
				}
				target_type = var->type;
			} else if (isa<MemberExpr>(a->target.get()) || isa<IndexExpr>(a->target.get())) {
				target_type = analyze_expr(a->target.get());
			} else if (isa<UnaryExpr>(a->target.get()) && as<UnaryExpr>(a->target.get())->op == TokenType::STAR) {
				target_type = analyze_expr(a->target.get()); // *ptr = value
			} else {
				logger.error(a->line, a->col, "Vế trái của phép gán không phải là lvalue hợp lệ");
				return Semantic::make_error();
			}

			auto val_type = analyze_expr(a->value.get());
			if (!target_type.can_assign_from(val_type)) {
				logger.error(a->line, a->col, "Không thể gán giá trị kiểu '" + val_type.to_string() +
				             "' cho đích kiểu '" + target_type.to_string() + "'");
			}

			return target_type;
		}

		// 4. Biểu thức Nhị phân: left op right
		if (isa<BinaryExpr>(expr)) {
			const auto* b = as<BinaryExpr>(expr);
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
						logger.error(b->line, b->col, "Không khớp kiểu trong phép toán số học: '" +
						             left_type.to_string() + "' và '" + right_type.to_string() +
						             "'. Cần dùng 'as' để ép kiểu tường minh.");
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
						logger.error(b->line, b->col, "Không khớp kiểu trong phép so sánh: '" +
						             left_type.to_string() + "' và '" + right_type.to_string() + "'");
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
						logger.error(b->line, b->col, "Không thể so sánh giữa 2 kiểu khác nhau: '" +
						             left_type.to_string() + "' và '" + right_type.to_string() + "'");
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
			const auto* u = as<UnaryExpr>(expr);
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
			const auto* c = as<CastExpr>(expr);
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

			if (!is_int_to_int && !is_ptr_to_ptr && !is_int_ptr_mix && !is_char_int_mix && !is_enum_int_mix) {
				logger.error(c->line, c->col, "Không thể ép kiểu từ '" + src_type.to_string() +
				             "' sang '" + target_type.to_string() + "'");
				return Semantic::make_error();
			}

			return target_type;
		}

		// 7. Lệnh gọi hàm: callee(args...)
		if (isa<CallExpr>(expr)) {
			const auto* c = as<CallExpr>(expr);
			if (!isa<IdentifierExpr>(c->callee.get())) {
				logger.error(c->line, c->col, "Chỉ hỗ trợ gọi hàm trực tiếp bằng tên");
				return Semantic::make_error();
			}

			const auto fn_name = as<IdentifierExpr>(c->callee.get())->name;
			auto it = functions.find(std::string(fn_name));
			if (it == functions.end()) {
				logger.error(c->line, c->col, "Hàm '" + std::string(fn_name) + "' chưa được khai báo");
				return Semantic::make_error();
			}

			const auto& fn_sym = it->second;
			if (c->args.size() != fn_sym.param_types.size()) {
				logger.error(c->line, c->col, "Hàm '" + std::string(fn_name) + "' mong đợi " +
				             std::to_string(fn_sym.param_types.size()) + " đối số, nhưng nhận được " +
				             std::to_string(c->args.size()));
				return fn_sym.return_type;
			}

			for (size_t i = 0; i < c->args.size(); ++i) {
				auto arg_type = analyze_expr(c->args[i].get());
				if (!fn_sym.param_types[i].can_assign_from(arg_type)) {
					logger.error(c->line, c->col, "Đối số " + std::to_string(i + 1) + " của hàm '" +
					             std::string(fn_name) + "' không khớp kiểu: mong đợi '" +
					             fn_sym.param_types[i].to_string() + "', nhận được '" + arg_type.to_string() + "'");
				}
			}

			return fn_sym.return_type;
		}

		// 8. Truy cập trường struct hoặc thành viên enum: object.field
		if (isa<MemberExpr>(expr)) {
			const auto* m = as<MemberExpr>(expr);

			// Kiểm tra nếu object là Identifier của một Enum (vd: Status.OK)
			if (isa<IdentifierExpr>(m->object.get())) {
				const auto id_name = std::string(as<IdentifierExpr>(m->object.get())->name);
				auto it_enum = enums.find(id_name);
				if (it_enum != enums.end()) {
					const auto member_name = std::string(m->member);
					auto it_m = it_enum->second.member_values.find(member_name);
					if (it_m == it_enum->second.member_values.end()) {
						logger.error(m->line, m->col, "Enum '" + id_name + "' không có thành viên nào tên là '" + member_name + "'");
						return Semantic::make_error();
					}
					return Semantic::make_enum(id_name, it_enum->second.underlying_type);
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

			std::string struct_name;
			if (obj_type.is_struct()) {
				struct_name = obj_type.struct_name;
			} else if (obj_type.is_pointer() && obj_type.pointee && obj_type.pointee->is_struct()) {
				struct_name = obj_type.pointee->struct_name;
			} else {
				logger.error(m->line, m->col, "Chỉ có thể truy cập trường '.' trên kiểu struct, con trỏ struct hoặc enum");
				return Semantic::make_error();
			}

			auto it = structs.find(struct_name);
			if (it == structs.end()) {
				logger.error(m->line, m->col, "Không tìm thấy định nghĩa của struct '" + struct_name + "'");
				return Semantic::make_error();
			}

			const auto field_name = std::string(m->member);
			auto it_f = it->second.field_types.find(field_name);
			if (it_f == it->second.field_types.end()) {
				logger.error(m->line, m->col, "Struct '" + struct_name + "' không có trường nào tên là '" + field_name + "'");
				return Semantic::make_error();
			}

			return it_f->second;
		}

		// 9. Chỉ mục mảng/con trỏ: ptr[index]
		if (isa<IndexExpr>(expr)) {
			const auto* idx = as<IndexExpr>(expr);
			auto target_type = analyze_expr(idx->target.get());
			auto index_type = analyze_expr(idx->index.get());

			if (target_type.is_error() || index_type.is_error()) return Semantic::make_error();

			if (!target_type.is_pointer() || !target_type.pointee) {
				logger.error(idx->line, idx->col, "Chỉ mục '[]' chỉ áp dụng cho kiểu con trỏ");
				return Semantic::make_error();
			}

			if (!index_type.is_integer()) {
				logger.error(idx->line, idx->col, "Chỉ mục trong '[]' phải là số nguyên");
				return Semantic::make_error();
			}

			return *target_type.pointee;
		}

		// 10. Nhóm ngoặc: (expr)
		if (isa<GroupExpr>(expr)) {
			return analyze_expr(as<GroupExpr>(expr)->expr.get());
		}

		return Semantic::make_error();
	}

	Semantic analyze_expr(const Expr* expr) {
		if (!expr) return Semantic::make_error();
		auto ty = compute_expr_type(expr);
		expr_types[expr] = ty;
		return ty;
	}

	Semantic get_expr_type(const Expr* expr) const {
		if (!expr) return Semantic::make_error();
		auto it = expr_types.find(expr);
		if (it != expr_types.end()) return it->second;
		return Semantic::make_error();
	}

	// ========================================================================
	// Hàm Phân tích Tổng thể
	// ========================================================================

	void analyze(const Program* program) {
		pass1_register_declarations(program);
		pass2_check_declarations(program);
	}
};
