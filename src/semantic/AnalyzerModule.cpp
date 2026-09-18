module;

#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module semantic.analyzer;

import ast;
import logger;
import semantic;
import semantic.symbol;

std::string Analyzer::get_decl_module(const Decl *decl) const {
	auto it = decl_modules.find(decl);
	if (it != decl_modules.end()) return it->second;
	return "";
}

template<typename TSymbol>
std::string Analyzer::resolve_symbol_helper(
	const StringMap<TSymbol> &symbol_table,
	const std::string_view raw_name,
	const std::string_view entity_type_name,
	const size_t line,
	const size_t col
) {
	const auto name = std::string(raw_name);

	// 1. Within current module
	if (!current_module.empty()) {
		const std::string local_qualified = current_module + "." + name;
		if (symbol_table.contains(local_qualified)) return local_qualified;
	}

	// 2. Lookup in current module's import table
	if (const auto it_imp = module_imports.find(current_module); it_imp != module_imports.end()) {
		const auto &imports = it_imp->second;
		if (const auto it = imports.find(raw_name); it != imports.end()) {
			const std::string &target = it->second;
			if (const auto it_sym = symbol_table.find(target); it_sym != symbol_table.end()) {
				const auto &sym = it_sym->second;
				if (!sym.is_pub && sym.module_name != current_module) {
					logger.error(
						line, col,
						std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name +
						"' is private and cannot be accessed from outside"
					);
				}
				return target;
			}
		}
	}

	// 3. Lookup in wildcard imports
	if (const auto it_wc = module_wildcards.find(current_module); it_wc != module_wildcards.end()) {
		for (const auto &w_mod: it_wc->second) {
			const std::string candidate = w_mod + "." + name;
			if (const auto it_sym = symbol_table.find(candidate); it_sym != symbol_table.end()) {
				const auto &sym = it_sym->second;
				if (!sym.is_pub && sym.module_name != current_module) {
					logger.error(
						line, col,
						std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name +
						"' is private and cannot be accessed from outside"
					);
				}
				return candidate;
			}
		}
	}

	// 4. Direct lookup (global / extern)
	if (const auto it_sym = symbol_table.find(raw_name); it_sym != symbol_table.end()) {
		const auto &sym = it_sym->second;
		if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
			logger.error(
				line, col,
				std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name +
				"' is private and cannot be accessed from outside"
			);
		}
		return name;
	}

	return "";
}

std::string Analyzer::resolve_function_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(functions, raw_name, "Function", line, col);
}

std::string Analyzer::resolve_struct_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(structs, raw_name, "Struct", line, col);
}

std::string Analyzer::resolve_generic_struct_name(const std::string_view raw_name, const size_t line, const size_t col) {
	const auto name = std::string(raw_name);

	// Check if bare symbol is ambiguous
	if (!current_module.empty()) {
		if (const auto it_amb = ambiguous_imports.find(current_module); it_amb != ambiguous_imports.end()) {
			if (it_amb->second.contains(name)) {
				logger.error(
					line, col,
					"Ambiguous reference to '" + name + "' due to multiple conflicting imports. Use a module prefix."
				);
				return "";
			}
		}
	}

	// 1. Within current module
	if (!current_module.empty()) {
		const std::string local_qualified = current_module + "." + name;
		if (generic_structs.contains(local_qualified)) return local_qualified;
	}

	// 2. Direct lookup
	if (const auto it = generic_structs.find(raw_name); it != generic_structs.end()) {
		return name;
	}

	// 3. Module imports
	if (const auto it_imp = module_imports.find(current_module); it_imp != module_imports.end()) {
		if (const auto it = it_imp->second.find(raw_name); it != it_imp->second.end()) {
			if (generic_structs.contains(it->second)) return it->second;
		}
	}

	// 4. Wildcard imports
	if (const auto it_wc = module_wildcards.find(current_module); it_wc != module_wildcards.end()) {
		for (const auto &w_mod: it_wc->second) {
			const std::string candidate = w_mod + "." + name;
			if (generic_structs.contains(candidate)) return candidate;
		}
	}

	return "";
}

std::string Analyzer::resolve_generic_function_name(const std::string_view raw_name, const size_t line, const size_t col) {
	const auto name = std::string(raw_name);

	// Check if bare symbol is ambiguous
	if (!current_module.empty()) {
		if (const auto it_amb = ambiguous_imports.find(current_module); it_amb != ambiguous_imports.end()) {
			if (it_amb->second.contains(name)) {
				logger.error(
					line, col,
					"Ambiguous reference to '" + name + "' due to multiple conflicting imports. Use a module prefix."
				);
				return "";
			}
		}
	}

	// 1. Within current module
	if (!current_module.empty()) {
		const std::string local_qualified = current_module + "." + name;
		if (generic_functions.contains(local_qualified)) return local_qualified;
	}

	// 2. Direct lookup
	if (const auto it = generic_functions.find(raw_name); it != generic_functions.end()) {
		return name;
	}

	// 3. Module imports
	if (const auto it_imp = module_imports.find(current_module); it_imp != module_imports.end()) {
		if (const auto it = it_imp->second.find(raw_name); it != it_imp->second.end()) {
			if (generic_functions.contains(it->second)) return it->second;
		}
	}

	// 4. Wildcard imports
	if (const auto it_wc = module_wildcards.find(current_module); it_wc != module_wildcards.end()) {
		for (const auto &w_mod: it_wc->second) {
			const std::string candidate = w_mod + "." + name;
			if (generic_functions.contains(candidate)) return candidate;
		}
	}

	return "";
}

std::string Analyzer::resolve_enum_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(enums, raw_name, "Enum", line, col);
}

std::string Analyzer::resolve_const_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(constants, raw_name, "Constant", line, col);
}

std::string Analyzer::resolve_trait_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(traits, raw_name, "Trait", line, col);
}

void Analyzer::pass0_index_modules(const Program *program) {
	decl_modules.clear();
	module_imports.clear();
	ambiguous_imports.clear();
	module_wildcards.clear();
	known_modules.clear();

	std::string active_mod;
	for (const auto &decl: program->declarations) {
		if (isa<ModuleDecl>(decl)) {
			active_mod = as<ModuleDecl>(decl)->full_path;
			if (!active_mod.empty()) {
				known_modules.insert(active_mod);
			}
		} else if (isa<UseDecl>(decl)) {
			const auto *u = as<UseDecl>(decl);
			decl_modules[u] = active_mod;
			const auto &full_path = u->full_path;
			if (u->is_wildcard) {
				module_wildcards[active_mod].push_back(std::string(full_path));
			} else {
				auto sym = std::string(u->symbol_name);
				auto target = std::string(full_path) + "." + std::string(sym);

				if (!u->alias.empty()) {
					auto alias = std::string(u->alias);
					module_imports[active_mod][alias] = target;
				} else {
					// Register last module prefix: e.g. b.B for use a.b.B;
					std::string last_mod_seg = std::string(full_path);
					if (size_t dot_pos = last_mod_seg.rfind('.'); dot_pos != std::string::npos) {
						last_mod_seg = last_mod_seg.substr(dot_pos + 1);
					}
					std::string prefixed_alias = last_mod_seg + "." + sym;

					if (module_imports[active_mod].contains(prefixed_alias) &&
					    module_imports[active_mod][prefixed_alias] != target) {
						logger.error(u->line, u->col, "Ambiguous import '" + prefixed_alias + "', explicit alias required with 'as'");
					} else {
						module_imports[active_mod][prefixed_alias] = target;
					}

					// For bare sym:
					if (module_imports[active_mod].contains(sym)) {
						if (module_imports[active_mod][sym] != target) {
							// Collision on bare symbol! Remove bare symbol so prefix is required
							module_imports[active_mod].erase(sym);
							ambiguous_imports[active_mod].insert(sym);
						}
					} else if (!ambiguous_imports[active_mod].contains(sym)) {
						module_imports[active_mod][sym] = target;
					}
				}
			}
		} else {
			decl_modules[decl] = active_mod;
		}
	}
}

void Analyzer::validate_use_declarations(const Program *program) {
	for (const auto &decl: program->declarations) {
		if (isa<UseDecl>(decl)) {
			const auto *u = as<UseDecl>(decl);
			std::string mod = get_decl_module(u);
			const auto &full_path = u->full_path;

			if (u->is_wildcard) {
				if (!known_modules.contains(full_path)) {
					bool mod_found = false;
					for (const auto &sym: functions | std::views::values) {
						if (sym.module_name == full_path) {
							mod_found = true;
							break;
						}
					}
					if (!mod_found) {
						for (const auto &sym: structs | std::views::values) {
							if (sym.module_name == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						for (const auto &sym: enums | std::views::values) {
							if (sym.module_name == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						for (const auto &sym: constants | std::views::values) {
							if (sym.module_name == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						for (const auto &sym: traits | std::views::values) {
							if (sym.module_name == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						for (const auto &st: generic_structs | std::views::values) {
							if (get_decl_module(st) == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						for (const auto &fn: generic_functions | std::views::values) {
							if (get_decl_module(fn) == full_path) {
								mod_found = true;
								break;
							}
						}
					}
					if (!mod_found) {
						logger.error(u->line, u->col, "module '" + std::string(full_path) + "' not found");
					}
				}
			} else {
				auto sym_name = std::string(u->symbol_name);
				auto target = std::string(full_path) + "." + std::string(sym_name);

				bool found = false;
				bool is_pub = false;

				if (functions.contains(target)) {
					found = true;
					is_pub = functions.at(target).is_pub;
				} else if (structs.contains(target)) {
					found = true;
					is_pub = structs.at(target).is_pub;
				} else if (generic_structs.contains(target)) {
					found = true;
					is_pub = generic_structs.at(target)->is_pub;
				} else if (generic_functions.contains(target)) {
					found = true;
					is_pub = generic_functions.at(target)->is_pub;
				} else if (traits.contains(target)) {
					found = true;
					is_pub = traits.at(target).is_pub;
				} else if (enums.contains(target)) {
					found = true;
					is_pub = enums.at(target).is_pub;
				} else if (constants.contains(target)) {
					found = true;
					is_pub = constants.at(target).is_pub;
				}

				if (!found) {
					logger.error(
						u->line, u->col,
						"Symbol '" + std::string(sym_name) + "' not found in module '" + std::string(full_path) + "'"
					);
				} else if (!is_pub && full_path != mod) {
					logger.error(
						u->line, u->col,
						"Symbol '" + std::string(sym_name) + "' in module '" + std::string(full_path) +
						"' is private and cannot be imported"
					);
				}
			}
		}
	}
}
