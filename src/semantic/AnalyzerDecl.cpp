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
		if (isa<StructDecl>(decl)) {
			const auto *st = as<StructDecl>(decl);
			std::string mod = get_decl_module(st);
			std::string qual_name = mod.empty() ? std::string(st->name) : mod + "." + std::string(st->name);

			if (!st->type_params.empty()) {
				if (generic_structs.contains(qual_name)) {
					logger.error(st->line, st->col, "Duplicate generic struct declaration '" + std::string(st->name) + "'");
					continue;
				}
				generic_structs[qual_name] = st;
				if (!mod.empty()) {
					generic_structs[to_llvm_name(qual_name)] = st;
				}
				continue;
			}

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
		if (isa<StructDecl>(decl)) {
			const auto *st = as<StructDecl>(decl);
			if (!st->type_params.empty()) continue; // Skip generic struct templates
			current_module = get_decl_module(st);
			std::string qual_name = current_module.empty()
				                        ? std::string(st->name)
				                        : current_module + "." + std::string(st->name);
			auto &sym = structs[qual_name];

			for (const auto &[name, type]: st->fields) {
				auto f_name = std::string(name);
				if (sym.field_types.contains(f_name)) {
					logger.error(
						st->line, st->col, "Duplicate field '" + f_name + "' in struct '" + sym.name + "'"
					);
					continue;
				}
				auto f_type = resolve_type(type);
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
				Semantic m_ret_sem = method->return_type ? resolve_type(method->return_type) : nullptr;
				std::vector<std::string> m_param_names;
				std::vector<Semantic> m_param_types;

				for (const auto &[name, type, is_mut, has_val]: method->params) {
					m_param_names.push_back(std::string(name));
					if (name == "self") {
						if (type) {
							m_param_types.push_back(resolve_type(type));
						} else if (is_mut) {
							m_param_types.push_back(
								make_pointer(make_struct(sym.name), true)
							);
						} else if (has_val) {
							m_param_types.push_back(
								make_pointer(make_struct(sym.name), false)
							);
						} else {
							m_param_types.push_back(make_struct(sym.name));
						}
					} else m_param_types.push_back(resolve_type(type));
				}

				if (!m_ret_sem && method->body && method->body->statements.size() == 1 && isa<ReturnStmt>(method->body->statements[0])) {
					enter_scope();
					for (size_t i = 0; i < m_param_names.size(); ++i) {
						VarSymbol p_sym{m_param_names[i], m_param_types[i], false, method->line, method->col};
						current_scope().variables[p_sym.name] = p_sym;
					}
					const auto *ret_stmt = as<ReturnStmt>(method->body->statements[0]);
					if (ret_stmt->value) {
						m_ret_sem = analyze_expr(ret_stmt->value);
					}
					exit_scope();
				}
				if (!m_ret_sem) {
					m_ret_sem = make_primitive(SemaType::VOID);
				}

				FnSymbol fn_sym = {
					.name = mangled_name,
					.param_types = m_param_types,
					.param_names = m_param_names,
					.return_type = m_ret_sem,
					.is_pub = method->is_pub,
					.module_name = current_module,
					.line = st->line,
					.col = st->col
				};

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
		if (isa<EnumDecl>(decl)) {
			const auto *e = as<EnumDecl>(decl);
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
				                      ? resolve_type(e->underlying_type)
				                      : make_primitive(SemaType::I32);
			sym.line = e->line;
			sym.col = e->col;

			if (!sym.underlying_type->is_integer()) {
				logger.error(e->line, e->col, "Enum underlying type must be an integer type");
				sym.underlying_type = make_primitive(SemaType::I32);
			}

			int64_t next_value = 0;
			for (const auto &[name, value, line, col]: e->members) {
				auto m_name = std::string(name);
				if (sym.member_values.contains(m_name)) {
					logger.error(line, col, "Duplicate member '" + m_name + "' in enum '" + std::string(e->name) + "'");
					continue;
				}

				if (value) {
					if (isa<LiteralExpr>(value)) {
						if (
							const auto *lit = as<LiteralExpr>(value);
							lit->literal_kind == LiteralKind::INT
						) {
							try {
								next_value = parse_kobel_int(lit->raw_text);
							} catch (...) {
								logger.error(line, col, "Invalid enum member initializer value");
							}
						} else {
							logger.error(line, col, "Enum member initializer must be an integer");
						}
					} else {
						logger.error(line, col, "Enum member initializer must be an integer constant");
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
		if (isa<ConstDecl>(decl)) {
			const auto *c = as<ConstDecl>(decl);
			std::string mod = get_decl_module(c);
			std::string qual_name = mod.empty() ? std::string(c->name) : mod + "." + std::string(c->name);

			if (constants.contains(qual_name)) {
				logger.error(c->line, c->col, "Duplicate constant declaration '" + std::string(c->name) + "'");
				continue;
			}

			current_module = mod;
			ConstSymbol sym = {
				.name = qual_name,
				.type = resolve_type(c->type),
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
		if (isa<FnDecl>(decl)) {
			register_function(as<FnDecl>(decl), get_decl_module(decl));
		} else if (isa<ExternBlock>(decl)) {
			for (
				const auto *ext = as<ExternBlock>(decl);
				const auto &fn: ext->declarations
			)
				register_function(fn, "");
		}
	}
}

void Analyzer::register_function(const FnDecl *fn, const std::string &mod) {
	const auto raw_name = std::string(fn->name);
	const std::string qual_name = mod.empty() || raw_name == "main" ? raw_name : mod + "." + raw_name;

	if (!fn->type_params.empty()) {
		if (generic_functions.contains(qual_name)) {
			logger.error(fn->line, fn->col, "Duplicate generic function declaration '" + raw_name + "'");
			return;
		}
		generic_functions[qual_name] = fn;
		if (!mod.empty()) {
			generic_functions[to_llvm_name(qual_name)] = fn;
		}
		return;
	}

	if (functions.contains(qual_name)) {
		logger.error(fn->line, fn->col, "Duplicate function declaration '" + raw_name + "'");
		return;
	}

	current_module = mod;
	Semantic ret_sem = nullptr;
	if (fn->return_type) {
		ret_sem = resolve_type(fn->return_type);
	}

	std::vector<std::string> param_names;
	std::vector<Semantic> param_types;
	for (const auto &p: fn->params) {
		param_names.push_back(std::string(p.name));
		param_types.push_back(resolve_type(p.type));
	}

	if (!ret_sem && fn->body && fn->body->statements.size() == 1 && isa<ReturnStmt>(fn->body->statements[0])) {
		enter_scope();
		for (size_t i = 0; i < param_names.size(); ++i) {
			VarSymbol p_sym{param_names[i], param_types[i], false, fn->line, fn->col};
			current_scope().variables[p_sym.name] = p_sym;
		}
		const auto *ret_stmt = as<ReturnStmt>(fn->body->statements[0]);
		if (ret_stmt->value) {
			ret_sem = analyze_expr(ret_stmt->value);
		}
		exit_scope();
	}
	if (!ret_sem) {
		ret_sem = make_primitive(SemaType::VOID);
	}

	FnSymbol sym = {
		.name = qual_name,
		.param_types = param_types,
		.param_names = param_names,
		.return_type = ret_sem,
		.is_pub = fn->is_pub,
		.module_name = mod,
		.line = fn->line,
		.col = fn->col
	};

	functions[qual_name] = sym;
	if (!mod.empty() && raw_name != "main") {
		functions[to_llvm_name(qual_name)] = sym;
	}
}

void Analyzer::pass2_check_declarations(const Program *program) {
	for (const auto &decl: program->declarations) {
		if (isa<FnDecl>(decl)) {
			const auto *fn = as<FnDecl>(decl);
			if (!fn->type_params.empty()) continue; // Generic templates are checked upon instantiation
			current_module = get_decl_module(fn);
			std::string qual_name = current_module.empty() || fn->name == "main"
				                        ? std::string(fn->name)
				                        : current_module + "." + std::string(fn->name);
			check_function(fn, qual_name);
		} else if (isa<StructDecl>(decl)) {
			const auto *st = as<StructDecl>(decl);
			if (!st->type_params.empty()) continue; // Generic templates are checked upon instantiation
			current_module = get_decl_module(st);
			std::string st_qual = current_module.empty()
				                      ? std::string(st->name)
				                      : current_module + "." + std::string(st->name);
			for (const auto &method: st->methods) {
				std::string mangled = st_qual + "_" + std::string(method->name);
				check_function(method, mangled);
			}
		} else if (isa<ConstDecl>(decl)) {
			const auto *c = as<ConstDecl>(decl);
			current_module = get_decl_module(c);
			std::string c_qual = current_module.empty()
				                     ? std::string(c->name)
				                     : current_module + "." + std::string(c->name);
			auto val_type = analyze_expr(c->value);
			auto expected_type = constants[c_qual].type;
			if (expected_type->is_integer() && val_type->is_integer() &&
			    isa<LiteralExpr>(c->value) &&
			    as<LiteralExpr>(c->value)->literal_kind == LiteralKind::INT) {
				val_type = expected_type;
				expr_types[c->value] = expected_type;
			}
			if (!expected_type->can_assign_from(val_type))
				logger.error(
					c->line, c->col, "Constant initializer type mismatch: expected '" +
					                 expected_type->to_string() + "', got '" + val_type->to_string() + "'"
				);
		}
	}

	// Check methods for instantiated generic structs and bodies for instantiated generic functions
	bool progress = true;
	size_t struct_idx = 0;
	size_t fn_idx = 0;
	while (progress) {
		progress = false;
		while (struct_idx < instantiated_struct_order.size()) {
			progress = true;
			const auto &inst_name = instantiated_struct_order[struct_idx++];
			std::string base_name = inst_name.substr(0, inst_name.find('<'));
			if (!generic_structs.contains(base_name)) continue;
			const auto *generic_st = generic_structs.at(base_name);
			if (generic_st->methods.empty()) continue;

			auto old_subst = active_type_substitutions;
			if (instantiated_type_maps.contains(inst_name)) {
				active_type_substitutions = instantiated_type_maps.at(inst_name);
			}

			for (const auto &method : generic_st->methods) {
				std::string mangled = to_llvm_name(inst_name) + "_" + std::string(method->name);
				if (functions.contains(mangled)) {
					check_function(method, mangled);
				}
			}

			active_type_substitutions = old_subst;
		}

		while (fn_idx < instantiated_function_order.size()) {
			progress = true;
			const auto &inst_name = instantiated_function_order[fn_idx++];
			const auto *fn_decl = instantiated_fn_decls.at(inst_name);
			auto old_mod = current_module;
			current_module = get_decl_module(fn_decl);

			auto old_subst = active_type_substitutions;
			if (instantiated_fn_type_maps.contains(inst_name)) {
				active_type_substitutions = instantiated_fn_type_maps.at(inst_name);
			}

			check_function(fn_decl, inst_name);

			active_type_substitutions = old_subst;
			current_module = old_mod;
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
		p_sym.is_mut = i < fn->params.size() ? fn->params[i].is_mut : false;
		p_sym.line = fn->line;
		p_sym.col = fn->col;
		current_scope().variables[p_sym.name] = p_sym;
	}

	// Analyze statements in function body
	for (const auto &stmt: fn->body->statements) {
		analyze_stmt(stmt);
	}

	// Check that non-void functions return on all control paths
	if (!sym.return_type->is_void() && !sym.return_type->is_error()) {
		if (!has_definite_return(fn->body)) {
			logger.error(
				fn->line, fn->col,
				"Function '" + std::string(fn->name) + "' missing return statement on all control paths"
			);
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
		for (const auto &s: b->statements) {
			if (has_definite_return(s)) return true;
		}
		return false;
	}

	if (isa<IfStmt>(stmt)) {
		const auto *i = as<IfStmt>(stmt);
		if (!i->else_branch) return false;
		return has_definite_return(i->then_branch) && has_definite_return(i->else_branch);
	}

	if (isa<WhenStmt>(stmt)) {
		const auto *w = as<WhenStmt>(stmt);
		bool has_else = false;
		for (const auto &arm: w->arms) {
			if (arm.is_else) has_else = true;
			if (!has_definite_return(arm.body)) return false;
		}
		return has_else && !w->arms.empty();
	}

	return false;
}
