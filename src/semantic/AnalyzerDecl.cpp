module;

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

void Analyzer::pass1_register_declarations(const Program *program) {
	// 1. Register Structs
	for (const auto &decl: program->declarations) {
		if (isa<StructDecl>(decl.get())) {
			const auto *st = as<StructDecl>(decl.get());
			std::string mod = get_decl_module(st);
			std::string qual_name = mod.empty() ? std::string(st->name) : mod + "." + std::string(st->name);

			if (structs.contains(qual_name)) {
				logger.error(st->line, st->col, "Duplicate struct declaration '" + std::string(st->name) + "'");
				continue;
			}

			StructSymbol sym = {
				.name = qual_name,
				.is_pub = st->is_pub,
				.module_name = mod,
				.line = st->line,
				.col = st->col
			};
			structs[qual_name] = sym;
			if (!mod.empty()) {
				structs[to_llvm_name(qual_name)] = sym;
			}
		}
	}

	// Register struct fields and methods
	for (const auto &decl: program->declarations) {
		if (isa<StructDecl>(decl.get())) {
			const auto *st = as<StructDecl>(decl.get());
			current_module = get_decl_module(st);
			std::string qual_name = current_module.empty() ? std::string(st->name) : current_module + "." + std::string(st->name);
			auto &sym = structs[qual_name];

			for (const auto &f: st->fields) {
				auto f_name = std::string(f.name);
				if (sym.field_types.contains(f_name)) {
					logger.error(
						st->line, st->col, "Duplicate field '" + f_name + "' in struct '" + sym.name + "'"
					);
					continue;
				}
				auto f_type = resolve_type(f.type.get());
				sym.field_types[f_name] = f_type;
				sym.field_order.push_back(f_name);
			}

			for (const auto &method: st->methods) {
				auto m_name = std::string(method->name);
				if (sym.methods.contains(m_name) || sym.field_types.contains(m_name)) {
					logger.error(
						method->line, method->col,
						"Duplicate method or field '" + m_name + "' in struct '" + sym.name + "'"
					);
					continue;
				}

				std::string mangled_name = sym.name + "_" + m_name;
				FnSymbol fn_sym = {
					.name = mangled_name,
					.return_type = resolve_type(method->return_type.get()),
					.is_pub = method->is_pub,
					.module_name = current_module,
					.line = st->line,
					.col = st->col
				};

				for (const auto &p: method->params) {
					fn_sym.param_names.push_back(std::string(p.name));
					if (p.name == "self") {
						if (p.type) {
							fn_sym.param_types.push_back(resolve_type(p.type.get()));
						} else if (p.is_mut) {
							fn_sym.param_types.push_back(
								Semantic::make_pointer(Semantic::make_struct(sym.name), true)
							);
						} else if (p.has_val) {
							fn_sym.param_types.push_back(
								Semantic::make_pointer(Semantic::make_struct(sym.name), false)
							);
						} else {
							fn_sym.param_types.push_back(Semantic::make_struct(sym.name));
						}
					} else fn_sym.param_types.push_back(resolve_type(p.type.get()));
				}

				sym.methods[m_name] = fn_sym;
				functions[mangled_name] = fn_sym;
				if (!current_module.empty()) {
					functions[to_llvm_name(mangled_name)] = fn_sym;
				}
			}

			if (!current_module.empty()) {
				structs[to_llvm_name(qual_name)] = sym;
			}
		}
	}

	// 2. Register Enums
	for (const auto &decl: program->declarations) {
		if (isa<EnumDecl>(decl.get())) {
			const auto *e = as<EnumDecl>(decl.get());
			std::string mod = get_decl_module(e);
			std::string qual_name = mod.empty() ? std::string(e->name) : mod + "." + std::string(e->name);

			if (enums.contains(qual_name) || structs.contains(qual_name)) {
				logger.error(e->line, e->col, "Duplicate type name '" + std::string(e->name) + "'");
				continue;
			}

			current_module = mod;
			EnumSymbol sym;
			sym.name = qual_name;
			sym.is_pub = e->is_pub;
			sym.module_name = mod;
			sym.underlying_type = e->underlying_type
				                      ? resolve_type(e->underlying_type.get())
				                      : Semantic::make_primitive(SemaType::I32);
			sym.line = e->line;
			sym.col = e->col;

			if (!sym.underlying_type.is_integer()) {
				logger.error(e->line, e->col, "Enum underlying type must be an integer type");
				sym.underlying_type = Semantic::make_primitive(SemaType::I32);
			}

			int64_t next_value = 0;
			for (const auto &m: e->members) {
				auto m_name = std::string(m.name);
				if (sym.member_values.contains(m_name)) {
					logger.error(m.line, m.col, "Duplicate member '" + m_name + "' in enum '" + std::string(e->name) + "'");
					continue;
				}

				if (m.value) {
					if (isa<LiteralExpr>(m.value.get())) {
						if (
							const auto *lit = as<LiteralExpr>(m.value.get());
							lit->literal_kind == LiteralKind::INT
						) {
							try {
								next_value = std::stoll(std::string(lit->raw_text), nullptr, 0);
							} catch (...) {
								logger.error(m.line, m.col, "Invalid enum member initializer value");
							}
						} else {
							logger.error(m.line, m.col, "Enum member initializer must be an integer");
						}
					} else {
						logger.error(m.line, m.col, "Enum member initializer must be an integer constant");
					}
				}

				sym.member_values[m_name] = next_value;
				next_value++;
			}

			enums[qual_name] = sym;
			if (!mod.empty()) {
				enums[to_llvm_name(qual_name)] = sym;
			}
		}
	}

	// 3. Register Constants
	for (const auto &decl: program->declarations) {
		if (isa<ConstDecl>(decl.get())) {
			const auto *c = as<ConstDecl>(decl.get());
			std::string mod = get_decl_module(c);
			std::string qual_name = mod.empty() ? std::string(c->name) : mod + "." + std::string(c->name);

			if (constants.contains(qual_name)) {
				logger.error(c->line, c->col, "Duplicate constant declaration '" + std::string(c->name) + "'");
				continue;
			}

			current_module = mod;
			ConstSymbol sym = {
				.name = qual_name,
				.type = resolve_type(c->type.get()),
				.is_pub = c->is_pub,
				.module_name = mod,
				.line = c->line,
				.col = c->col
			};
			constants[qual_name] = sym;
			if (!mod.empty()) {
				constants[to_llvm_name(qual_name)] = sym;
			}
		}
	}

	// 4. Register Functions (including extern blocks)
	for (const auto &decl: program->declarations) {
		if (isa<FnDecl>(decl.get())) {
			register_function(as<FnDecl>(decl.get()), get_decl_module(decl.get()));
		} else if (isa<ExternBlock>(decl.get())) {
			for (
				const auto *ext = as<ExternBlock>(decl.get());
				const auto &fn: ext->declarations
			)
				register_function(fn.get(), "");
		}
	}
}

void Analyzer::register_function(const FnDecl *fn, const std::string &mod) {
	const auto raw_name = std::string(fn->name);
	const std::string qual_name = (mod.empty() || raw_name == "main") ? raw_name : mod + "." + raw_name;

	if (functions.contains(qual_name)) {
		logger.error(fn->line, fn->col, "Duplicate function declaration '" + raw_name + "'");
		return;
	}

	current_module = mod;
	FnSymbol sym = {
		.name = qual_name,
		.return_type = resolve_type(fn->return_type.get()),
		.is_pub = fn->is_pub,
		.module_name = mod,
		.line = fn->line,
		.col = fn->col
	};

	for (const auto &p: fn->params) {
		sym.param_names.push_back(std::string(p.name));
		sym.param_types.push_back(resolve_type(p.type.get()));
	}

	functions[qual_name] = sym;
	if (!mod.empty() && raw_name != "main") {
		functions[to_llvm_name(qual_name)] = sym;
	}
}

void Analyzer::pass2_check_declarations(const Program *program) {
	for (const auto &decl: program->declarations) {
		if (isa<FnDecl>(decl.get())) {
			const auto *fn = as<FnDecl>(decl.get());
			current_module = get_decl_module(fn);
			std::string qual_name = (current_module.empty() || fn->name == "main")
				? std::string(fn->name)
				: current_module + "." + std::string(fn->name);
			check_function(fn, qual_name);
		} else if (isa<StructDecl>(decl.get())) {
			const auto *st = as<StructDecl>(decl.get());
			current_module = get_decl_module(st);
			std::string st_qual = current_module.empty()
				? std::string(st->name)
				: current_module + "." + std::string(st->name);
			for (const auto &method: st->methods) {
				std::string mangled = st_qual + "_" + std::string(method->name);
				check_function(method.get(), mangled);
			}
		} else if (isa<ConstDecl>(decl.get())) {
			const auto *c = as<ConstDecl>(decl.get());
			current_module = get_decl_module(c);
			std::string c_qual = current_module.empty()
				? std::string(c->name)
				: current_module + "." + std::string(c->name);
			auto val_type = analyze_expr(c->value.get());
			auto expected_type = constants[c_qual].type;
			if (expected_type.is_integer() && val_type.is_integer() &&
			    isa<LiteralExpr>(c->value.get()) &&
			    as<LiteralExpr>(c->value.get())->literal_kind == LiteralKind::INT) {
				val_type = expected_type;
				expr_types[c->value.get()] = expected_type;
			}
			if (!expected_type.can_assign_from(val_type))
				logger.error(
					c->line, c->col, "Constant initializer type mismatch: expected '" +
					                 expected_type.to_string() + "', got '" + val_type.to_string() + "'"
				);
		}
	}
}

void Analyzer::check_function(const FnDecl *fn, const std::string &fn_lookup_name) {
	if (!fn->body) return; // Function prototype without body

	const auto &sym = functions[fn_lookup_name];
	current_function_return_type = sym.return_type;

	enter_scope(); // Function level scope

	// Register function parameters
	for (size_t i = 0; i < sym.param_names.size(); ++i) {
		VarSymbol p_sym;
		p_sym.name = sym.param_names[i];
		p_sym.type = sym.param_types[i];
		p_sym.is_mut = (i < fn->params.size()) ? fn->params[i].is_mut : false;
		p_sym.line = fn->line;
		p_sym.col = fn->col;
		current_scope().variables[p_sym.name] = p_sym;
	}

	// Analyze statements in function body
	for (const auto &stmt: fn->body->statements) {
		analyze_stmt(stmt.get());
	}

	// Check that non-void functions return on all control paths
	if (!sym.return_type.is_void() && !sym.return_type.is_error()) {
		if (!has_definite_return(fn->body.get())) {
			logger.error(fn->line, fn->col, "Function '" + std::string(fn->name) + "' missing return statement on all control paths");
		}
	}

	exit_scope();
	current_function_return_type.reset();
}

bool Analyzer::has_definite_return(const Stmt *stmt) {
	if (!stmt) return false;

	if (isa<ReturnStmt>(stmt)) return true;

	if (isa<BlockStmt>(stmt)) {
		const auto *b = as<BlockStmt>(stmt);
		for (const auto &s : b->statements) {
			if (has_definite_return(s.get())) return true;
		}
		return false;
	}

	if (isa<IfStmt>(stmt)) {
		const auto *i = as<IfStmt>(stmt);
		if (!i->else_branch) return false;
		return has_definite_return(i->then_branch.get()) && has_definite_return(i->else_branch.get());
	}

	return false;
}
