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

void Analyzer::collect_trait_methods(
	const std::string &trait_name,
	StringMap<const FnDecl *> &out_required,
	StringMap<const FnDecl *> &out_defaults,
	std::vector<std::string> &out_all_traits,
	std::unordered_set<std::string> &visited
) {
	if (visited.contains(trait_name)) return;
	visited.insert(trait_name);
	out_all_traits.push_back(trait_name);

	if (!traits.contains(trait_name)) return;
	const auto &sym = traits.at(trait_name);

	for (const auto &b : sym.base_traits) {
		std::string resolved_b = resolve_trait_name(b, sym.line, sym.col);
		if (!resolved_b.empty()) {
			collect_trait_methods(resolved_b, out_required, out_defaults, out_all_traits, visited);
		}
	}

	for (const auto &[m_name, fn_decl] : sym.default_methods) {
		out_required.erase(m_name);
		out_defaults[m_name] = fn_decl;
	}

	for (const auto &[m_name, fn_decl] : sym.required_methods) {
		if (!out_defaults.contains(m_name)) {
			out_required[m_name] = fn_decl;
		}
	}
}

void Analyzer::check_and_apply_struct_traits(
	const StructDecl *st,
	const std::string &qual_name
) {
	if (st->traits.empty()) {
		for (const auto &method : st->methods) {
			if (method->is_override) {
				logger.error(
					method->line, method->col,
					"Method '" + std::string(method->name) + "' in struct '" + qual_name +
					"' is marked 'override' but struct does not implement any traits"
				);
			}
		}
		return;
	}

	auto &sym = structs[qual_name];
	std::string mod = get_decl_module(st);
	current_self_type = make_struct(qual_name);

	StringMap<const FnDecl *> all_required;
	StringMap<const FnDecl *> all_defaults;
	std::vector<std::string> all_traits;
	std::unordered_set<std::string> visited;

	for (const auto &raw_tr : st->traits) {
		std::string tr_name = resolve_trait_name(raw_tr, st->line, st->col);
		if (tr_name.empty()) {
			logger.error(
				st->line, st->col,
				"Unknown trait '" + std::string(raw_tr) + "' in struct '" + sym.name + "'"
			);
			continue;
		}
		collect_trait_methods(tr_name, all_required, all_defaults, all_traits, visited);
	}

	struct_traits[qual_name] = all_traits;
	if (!mod.empty()) {
		struct_traits[to_llvm_name(qual_name)] = all_traits;
	}

	auto build_fn_symbol = [&](const FnDecl *fn_decl, const std::string &mangled) -> FnSymbol {
		std::vector<Semantic> p_types;
		std::vector<std::string> p_names;

		for (const auto &[p_name, p_type, is_mut, has_val] : fn_decl->params) {
			p_names.push_back(std::string(p_name));
			if (p_name == "self") {
				if (is_mut) {
					p_types.push_back(make_pointer(current_self_type, true));
				} else if (has_val) {
					p_types.push_back(make_pointer(current_self_type, false));
				} else {
					p_types.push_back(current_self_type);
				}
			} else {
				p_types.push_back(resolve_type(p_type));
			}
		}

		Semantic ret_type = fn_decl->return_type ? resolve_type(fn_decl->return_type) : make_primitive(SemaType::VOID);
		return FnSymbol{
			.name = mangled,
			.param_types = p_types,
			.param_names = p_names,
			.return_type = ret_type,
			.is_pub = fn_decl->is_pub,
			.module_name = mod,
			.line = fn_decl->line,
			.col = fn_decl->col
		};
	};

	// 1. Check all required methods
	for (const auto &[req_name, req_decl] : all_required) {
		if (!sym.methods.contains(req_name)) {
			if (all_defaults.contains(req_name)) {
				const auto *def_decl = all_defaults.at(req_name);
				std::string mangled = sym.name + "_" + req_name;
				FnSymbol inh_sym = build_fn_symbol(def_decl, mangled);
				sym.methods[req_name] = inh_sym;
				functions[mangled] = inh_sym;
				if (!mod.empty()) {
					functions[to_llvm_name(mangled)] = inh_sym;
				}
				struct_default_methods[qual_name].push_back({req_name, def_decl});
			} else {
				logger.error(
					st->line, st->col,
					"Struct '" + sym.name + "' does not implement required method '" + req_name + "'"
				);
			}
		}
	}

	// 2. Inherit remaining default methods
	for (const auto &[def_name, def_decl] : all_defaults) {
		if (!sym.methods.contains(def_name)) {
			std::string mangled = sym.name + "_" + def_name;
			FnSymbol inh_sym = build_fn_symbol(def_decl, mangled);
			sym.methods[def_name] = inh_sym;
			functions[mangled] = inh_sym;
			if (!mod.empty()) {
				functions[to_llvm_name(mangled)] = inh_sym;
			}
			struct_default_methods[qual_name].push_back({def_name, def_decl});
		}
	}

	// 3. Verify signatures and 'override' modifier
	for (const auto &method : st->methods) {
		std::string m_name = std::string(method->name);
		bool is_in_trait = all_required.contains(m_name) || all_defaults.contains(m_name);

		if (method->is_override && !is_in_trait) {
			logger.error(
				method->line, method->col,
				"Method '" + m_name + "' in struct '" + sym.name +
				"' is marked 'override' but does not override any trait method"
			);
			continue;
		}

		if (!method->is_override && is_in_trait) {
			logger.error(
				method->line, method->col,
				"Method '" + m_name + "' in struct '" + sym.name +
				"' overrides a trait method but is missing 'override' modifier"
			);
		}

		if (is_in_trait) {
			sym.methods[m_name].is_pub = true;
			std::string mangled = sym.name + "_" + m_name;
			if (functions.contains(mangled)) {
				functions[mangled].is_pub = true;
			}
			if (!mod.empty()) {
				if (functions.contains(to_llvm_name(mangled))) {
					functions[to_llvm_name(mangled)].is_pub = true;
				}
			}

			const auto *expected_decl = all_required.contains(m_name) ? all_required.at(m_name) : all_defaults.at(m_name);
			FnSymbol expected = build_fn_symbol(expected_decl, "");
			const auto &actual = sym.methods.at(m_name);

			if (actual.param_types.size() != expected.param_types.size()) {
				logger.error(
					method->line, method->col,
					"Method '" + m_name + "' has " + std::to_string(actual.param_types.size()) +
					" parameter(s), but trait method expects " + std::to_string(expected.param_types.size())
				);
				continue;
			}

			if (!actual.param_types.empty() && !expected.param_types.empty()) {
				bool exp_ptr = expected.param_types[0]->is_pointer();
				bool exp_mut = exp_ptr && expected.param_types[0]->is_mut_pointer;
				bool act_ptr = actual.param_types[0]->is_pointer();
				bool act_mut = act_ptr && actual.param_types[0]->is_mut_pointer;

				if (exp_ptr != act_ptr || exp_mut != act_mut) {
					std::string exp_str = exp_ptr ? (exp_mut ? "var self" : "val self") : "self";
					std::string act_str = act_ptr ? (act_mut ? "var self" : "val self") : "self";
					logger.error(
						method->line, method->col,
						"Method '" + m_name + "' receiver mode '" + act_str +
						"' does not match trait method (expected '" + exp_str + "')"
					);
				}
			}

			for (size_t i = 1; i < expected.param_types.size(); ++i) {
				if (actual.param_types[i] != expected.param_types[i]) {
					logger.error(
						method->line, method->col,
						"Parameter " + std::to_string(i) + " of method '" + m_name +
						"' has type '" + actual.param_types[i]->to_string() +
						"', but trait method expects '" + expected.param_types[i]->to_string() + "'"
					);
				}
			}

			if (actual.return_type != expected.return_type) {
				logger.error(
					method->line, method->col,
					"Method '" + m_name + "' return type '" + actual.return_type->to_string() +
					"' does not match trait method (expected '" + expected.return_type->to_string() + "')"
				);
			}
		}
	}

	current_self_type = nullptr;
}

void Analyzer::pass1_register_declarations(const Program *program) {
	// 0. Register Traits
	for (const auto &decl : program->declarations) {
		if (isa<TraitDecl>(decl)) {
			const auto *tr = as<TraitDecl>(decl);
			std::string mod = get_decl_module(tr);
			std::string qual_name = mod.empty() ? std::string(tr->name) : mod + "." + std::string(tr->name);

			if (traits.contains(qual_name)) {
				logger.error(tr->line, tr->col, "Duplicate trait declaration '" + std::string(tr->name) + "'");
				continue;
			}

			TraitSymbol sym = {
				.name = qual_name,
				.is_pub = tr->is_pub,
				.module_name = mod,
				.line = tr->line,
				.col = tr->col
			};

			for (const auto &b : tr->bases) {
				sym.base_traits.push_back(std::string(b));
			}

			for (const auto &method : tr->methods) {
				auto m_name = std::string(method->name);
				if (sym.required_methods.contains(m_name) || sym.default_methods.contains(m_name)) {
					logger.error(method->line, method->col, "Duplicate method '" + m_name + "' in trait '" + sym.name + "'");
					continue;
				}

				if (method->body) {
					sym.default_methods[m_name] = method;
				} else {
					sym.required_methods[m_name] = method;
				}
			}

			traits[qual_name] = sym;
			if (!mod.empty()) {
				traits[to_llvm_name(qual_name)] = sym;
			}
		}
	}

	// 1. Register Structs
	for (const auto &decl: program->declarations) {
		if (isa<StructDecl>(decl)) {
			const auto *st = as<StructDecl>(decl);
			std::string mod = get_decl_module(st);
			std::string qual_name = mod.empty() ? std::string(st->name) : mod + "." + std::string(st->name);

			all_struct_decls[qual_name] = const_cast<StructDecl *>(st);
			if (!mod.empty()) {
				all_struct_decls[to_llvm_name(qual_name)] = const_cast<StructDecl *>(st);
			}
			all_struct_decls[std::string(st->name)] = const_cast<StructDecl *>(st);

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

	// 1.5. Bind Impl blocks to structs
	for (const auto &decl: program->declarations) {
		if (isa<ImplDecl>(decl)) {
			const auto *impl = as<ImplDecl>(decl);
			std::string mod = get_decl_module(impl);
			current_module = mod;

			std::string qual_name;
			if (std::string gen_qual = resolve_generic_struct_name(impl->struct_name, impl->line, impl->col); !gen_qual.empty()) {
				qual_name = gen_qual;
			} else if (std::string st_qual = resolve_struct_name(impl->struct_name, impl->line, impl->col); !st_qual.empty()) {
				qual_name = st_qual;
			} else {
				std::string local_qual = mod.empty() ? std::string(impl->struct_name) : mod + "." + std::string(impl->struct_name);
				if (all_struct_decls.contains(local_qual)) {
					qual_name = local_qual;
				} else if (all_struct_decls.contains(std::string(impl->struct_name))) {
					qual_name = std::string(impl->struct_name);
				}
			}

			if (qual_name.empty() || !all_struct_decls.contains(qual_name)) {
				logger.error(impl->line, impl->col, "Cannot find struct '" + std::string(impl->struct_name) + "' for 'impl'");
				continue;
			}

			auto *target_st = all_struct_decls[qual_name];

			// Merge methods
			std::vector<FnDecl *> merged_methods(target_st->methods.begin(), target_st->methods.end());
			for (auto *m : impl->methods) {
				merged_methods.push_back(m);
				decl_modules[m] = mod;
			}
			merged_methods_storage.push_back(std::move(merged_methods));
			target_st->methods = merged_methods_storage.back();

			// Merge trait if specified
			if (!impl->trait_name.empty()) {
				std::string tr_qual = resolve_trait_name(impl->trait_name, impl->line, impl->col);
				if (tr_qual.empty()) {
					logger.error(impl->line, impl->col, "Unknown trait '" + std::string(impl->trait_name) + "' in 'impl'");
					continue;
				}
				resolved_trait_names_storage.push_back(std::move(tr_qual));
				std::string_view tr_sv = resolved_trait_names_storage.back();

				std::vector<std::string_view> merged_traits(target_st->traits.begin(), target_st->traits.end());
				merged_traits.push_back(tr_sv);
				merged_traits_storage.push_back(std::move(merged_traits));
				target_st->traits = merged_traits_storage.back();
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

			for (const auto &[name, type, is_pub]: st->fields) {
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
				sym.field_pub[f_name] = is_pub;
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

			check_and_apply_struct_traits(st, qual_name);

			if (!current_module.empty()) {
				structs[to_llvm_name(qual_name)] = structs[qual_name];
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
				auto old_mod = current_module;
				current_module = decl_modules.contains(method) ? decl_modules.at(method) : get_decl_module(st);
				check_function(method, mangled);
				current_module = old_mod;
			}
			if (struct_default_methods.contains(st_qual)) {
				for (const auto &inh : struct_default_methods.at(st_qual)) {
					std::string mangled = st_qual + "_" + inh.method_name;
					auto old_mod = current_module;
					current_module = get_decl_module(inh.fn_decl);
					check_function(inh.fn_decl, mangled);
					current_module = old_mod;
				}
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
			if (!generic_structs.contains(base_name)) {
				if (std::string resolved = resolve_generic_struct_name(base_name); !resolved.empty()) {
					base_name = resolved;
				}
			}
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
					auto old_mod = current_module;
					current_module = decl_modules.contains(method) ? decl_modules.at(method) : get_decl_module(generic_st);
					check_function(method, mangled);
					current_module = old_mod;
				}
			}

			if (struct_default_methods.contains(inst_name)) {
				for (const auto &inh : struct_default_methods.at(inst_name)) {
					std::string mangled = to_llvm_name(inst_name) + "_" + inh.method_name;
					if (functions.contains(mangled)) {
						auto old_mod = current_module;
						current_module = get_decl_module(inh.fn_decl);
						check_function(inh.fn_decl, mangled);
						current_module = old_mod;
					}
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
