module;

#include <string>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

void Analyzer::analyze_stmt(const Stmt *stmt) {
	if (!stmt) return;

	// 1. Variable declaration: val / var
	if (isa<VarDeclStmt>(stmt)) {
		const auto *v = as<VarDeclStmt>(stmt);
		const auto name = std::string(v->name);

		// Explicit type annotation is required
		if (!v->type_annotation) {
			logger.error(v->line, v->col, "Explicit type annotation is required for variable '" + name + "'");
			return;
		}

		auto declared_type = resolve_type(v->type_annotation);

		// Check initializer if present
		if (v->initializer) {
			auto init_type = analyze_expr(v->initializer);

			// Contextual integer literal typing:
			if (declared_type->is_integer() && init_type->is_integer() &&
			    isa<LiteralExpr>(v->initializer) &&
			    as<LiteralExpr>(v->initializer)->literal_kind == LiteralKind::INT) {
				init_type = declared_type;
				expr_types[v->initializer] = declared_type;
			}

			// Infer array size if declared as Array<T> (size == 0)
			if (declared_type->is_array() && init_type->is_array()) {
				if (declared_type->array_size == 0) {
					declared_type->array_size = (init_type->array_size);
					if (isa<ArrayType>(v->type_annotation)) {
						as<ArrayType>(v->type_annotation)->size = init_type->array_size;
					}
				}
			}

			if (!declared_type->can_assign_from(init_type)) {
				logger.error(
					v->line, v->col, "Cannot initialize variable '" + name + "' of type '" +
					                 declared_type->to_string() + "' with value of type '" + init_type->to_string() +
					                 "'"
				);
			}
		}

		// Check duplicate in the same scope
		if (current_scope().variables.contains(name)) {
			logger.error(v->line, v->col, "Variable '" + name + "' was already declared in this scope");
			return;
		}

		VarSymbol sym = {name, declared_type, v->is_mut, v->line, v->col};
		current_scope().variables[name] = sym;
		return;
	}

	// 2. Block: { ... }
	if (isa<BlockStmt>(stmt)) {
		const auto *b = as<BlockStmt>(stmt);
		enter_scope();
		for (const auto &s: b->statements) {
			analyze_stmt(s);
		}
		exit_scope();
		return;
	}

	// 3. If statement: if (cond) { ... } else { ... }
	if (isa<IfStmt>(stmt)) {
		const auto *i = as<IfStmt>(stmt);
		if (
			auto cond_type = analyze_expr(i->condition);
			!cond_type->is_bool() && !cond_type->is_error()
		)
			logger.error(
				i->line, i->col,
				"Condition of 'if' statement must be of type 'bool', got '" + cond_type->to_string() + "'"
			);
		analyze_stmt(i->then_branch);
		if (i->else_branch) analyze_stmt(i->else_branch);
		return;
	}

	// 4. While loop: while (cond) { ... }
	if (isa<WhileStmt>(stmt)) {
		const auto *w = as<WhileStmt>(stmt);
		if (
			auto cond_type = analyze_expr(w->condition);
			!cond_type->is_bool() && !cond_type->is_error()
		)
			logger.error(
				w->line, w->col, "Condition of 'while' statement must be of type 'bool', got '" + cond_type->to_string() + "'"
			);
		loop_depth++;
		analyze_stmt(w->body);
		loop_depth--;
		return;
	}

	// 5. Return statement: return expr;
	if (isa<ReturnStmt>(stmt)) {
		const auto *r = as<ReturnStmt>(stmt);
		if (!current_function_return_type.has_value()) {
			logger.error(r->line, r->col, "Return statement is only valid inside a function body");
			return;
		}

		const auto expected = current_function_return_type.value();
		if (r->value) {
			auto val_type = analyze_expr(r->value);
			if (expected->is_integer() && val_type->is_integer() &&
			    isa<LiteralExpr>(r->value) &&
			    as<LiteralExpr>(r->value)->literal_kind == LiteralKind::INT) {
				val_type = expected;
				expr_types[r->value] = expected;
			}
			if (!expected->can_assign_from(val_type))
				logger.error(
					r->line, r->col,
					"Return value type '" + val_type->to_string() +
					"' does not match expected function return type '" + expected->to_string() + "'"
				);
		} else if (!expected->is_void())
			logger.error(
				r->line, r->col,
				"Function expects return type '" + expected->to_string() +
				"', cannot return void"
			);
		return;
	}

	// 6. Break & Continue
	if (isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt)) {
		if (loop_depth <= 0) {
			logger.error(
				stmt->line, stmt->col,
				"'break'/'continue' statement is only allowed inside a loop"
			);
		}
		return;
	}

	// 7. Expression statement: expr;
	if (isa<ExprStmt>(stmt)) {
		const auto *e = as<ExprStmt>(stmt);
		analyze_expr(e->expr);
		return;
	}

	// 8. When statement: when (cond) { ... }
	if (isa<WhenStmt>(stmt)) {
		analyze_when_stmt(as<WhenStmt>(stmt));
		return;
	}
}

void Analyzer::analyze_when_stmt(const WhenStmt *stmt) {
	if (!stmt) return;

	Semantic cond_type = nullptr;
	if (stmt->condition) {
		cond_type = analyze_expr(stmt->condition);
	}

	for (const auto& arm : stmt->arms) {
		if (!arm.is_else) {
			for (const auto* pat : arm.patterns) {
				auto pat_type = analyze_expr(pat);
				if (cond_type) {
					if (cond_type->is_integer() && pat_type->is_integer() &&
					    isa<LiteralExpr>(pat) && as<LiteralExpr>(pat)->literal_kind == LiteralKind::INT) {
						pat_type = cond_type;
						expr_types[pat] = cond_type;
					}
					if (cond_type->is_enum() && pat_type->is_enum()) {
						if (cond_type != pat_type) {
							logger.error(pat->line, pat->col,
								"Pattern enum '" + pat_type->to_string() + "' does not match when condition enum '" + cond_type->to_string() + "'");
						}
					} else if (!cond_type->can_assign_from(pat_type) && !pat_type->can_assign_from(cond_type)) {
						logger.error(pat->line, pat->col,
							"Pattern type '" + pat_type->to_string() + "' is incompatible with when condition type '" + cond_type->to_string() + "'");
					}
				} else {
					if (!pat_type->is_bool() && !pat_type->is_error()) {
						logger.error(pat->line, pat->col,
							"When condition pattern must be of type 'bool', got '" + pat_type->to_string() + "'");
					}
				}
			}
		}

		if (arm.body) {
			analyze_stmt(arm.body);
		}
	}
}



