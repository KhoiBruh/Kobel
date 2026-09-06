module;

#include <string>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

void Analyzer::analyze_stmt(const Stmt *stmt) {
	if (!stmt) return;

	// 1. Khai báo biến: val / var
	if (isa<VarDeclStmt>(stmt)) {
		const auto *v = as<VarDeclStmt>(stmt);
		const auto name = std::string(v->name);

		// Quy tắc 2: Bắt buộc ghi kiểu tường minh
		if (!v->type_annotation) {
			logger.error(v->line, v->col, "Bắt buộc phải ghi kiểu dữ liệu tường minh cho biến '" + name + "'");
			return;
		}

		auto declared_type = resolve_type(v->type_annotation.get());

		// Kiểm tra giá trị khởi tạo nếu có
		if (v->initializer) {
			auto init_type = analyze_expr(v->initializer.get());

			// Suy luận kích thước mảng nếu khai báo là Array<T> (size == 0)
			if (declared_type.is_array() && init_type.is_array()) {
				if (declared_type.array_size == 0) {
					declared_type.array_size = init_type.array_size;
					if (isa<ArrayType>(v->type_annotation.get())) {
						as<ArrayType>(v->type_annotation.get())->size = init_type.array_size;
					}
				}
			}

			if (!declared_type.can_assign_from(init_type)) {
				logger.error(
					v->line, v->col, "Không thể khởi tạo biến '" + name + "' kiểu '" +
					                 declared_type.to_string() + "' bằng giá trị kiểu '" + init_type.to_string() +
					                 "'"
				);
			}
		}

		// Kiểm tra trùng lặp trong cùng một scope
		if (current_scope().variables.contains(name)) {
			logger.error(v->line, v->col, "Biến '" + name + "' đã được khai báo trước đó trong cùng phạm vi");
			return;
		}

		VarSymbol sym = {name, declared_type, v->is_mut, v->line, v->col};
		current_scope().variables[name] = sym;
		return;
	}

	// 2. Khối lệnh: { ... }
	if (isa<BlockStmt>(stmt)) {
		const auto *b = as<BlockStmt>(stmt);
		enter_scope();
		for (const auto &s: b->statements) {
			analyze_stmt(s.get());
		}
		exit_scope();
		return;
	}

	// 3. Câu lệnh if: if (cond) { ... } else { ... }
	if (isa<IfStmt>(stmt)) {
		const auto *i = as<IfStmt>(stmt);
		if (
			auto cond_type = analyze_expr(i->condition.get());
			!cond_type.is_bool() && !cond_type.is_error()
		)
			logger.error(
				i->line, i->col,
				"Điều kiện if phải có kiểu 'bool', gặp kiểu '" + cond_type.to_string() + "'"
			);
		analyze_stmt(i->then_branch.get());
		if (i->else_branch) analyze_stmt(i->else_branch.get());
		return;
	}

	// 4. Vòng lặp while: while (cond) { ... }
	if (isa<WhileStmt>(stmt)) {
		const auto *w = as<WhileStmt>(stmt);
		if (
			auto cond_type = analyze_expr(w->condition.get());
			!cond_type.is_bool() && !cond_type.is_error()
		)
			logger.error(
				w->line, w->col, "Điều kiện while phải có kiểu 'bool', gặp kiểu '" + cond_type.to_string() + "'"
			);
		loop_depth++;
		analyze_stmt(w->body.get());
		loop_depth--;
		return;
	}

	// 5. Câu lệnh return: return expr;
	if (isa<ReturnStmt>(stmt)) {
		const auto *r = as<ReturnStmt>(stmt);
		if (!current_function_return_type.has_value()) {
			logger.error(r->line, r->col, "Lệnh 'return' chỉ hợp lệ bên trong thân hàm");
			return;
		}

		const auto expected = current_function_return_type.value();
		if (r->value) {
			if (
				auto val_type = analyze_expr(r->value.get());
				!expected.can_assign_from(val_type)
			)
				logger.error(
					r->line, r->col,
					"Kiểu giá trị trả về '" + val_type.to_string() +
					"' không khớp với kiểu hàm mong đợi '" + expected.to_string() + "'"
				);
		} else if (!expected.is_void())
			logger.error(
				r->line, r->col,
				"Hàm mong đợi trả về kiểu '" + expected.to_string() +
				"', không được dùng lệnh return rỗng"
			);
		return;
	}

	// 6. Break & Continue
	if (isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt)) {
		if (loop_depth <= 0) {
			logger.error(
				stmt->line, stmt->col,
				"Lệnh 'break'/'continue' chỉ được phép nằm bên trong vòng lặp while"
			);
		}
		return;
	}

	// 7. Câu lệnh biểu thức: expr;
	if (isa<ExprStmt>(stmt)) {
		const auto *e = as<ExprStmt>(stmt);
		analyze_expr(e->expr.get());
	}
}
