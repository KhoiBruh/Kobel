module;

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

std::string Analyzer::get_decl_module(const Decl* decl) const {
	auto it = decl_modules.find(decl);
	if (it != decl_modules.end()) return it->second;
	return "";
}

std::string Analyzer::join_path(const std::vector<std::string_view>& path) {
	std::string res;
	for (size_t i = 0; i < path.size(); ++i) {
		if (i > 0) res += ".";
		res += path[i];
	}
	return res;
}

template <typename TSymbol>
std::string Analyzer::resolve_symbol_helper(
	const StringMap<TSymbol>& symbol_table,
	const std::string_view raw_name,
	const std::string_view entity_type_name,
	const size_t line,
	const size_t col
) {
	const std::string name = std::string(raw_name);

	// 1. Within current module
	if (!current_module.empty()) {
		const std::string local_qualified = current_module + "." + name;
		if (symbol_table.contains(local_qualified)) return local_qualified;
	}

	// 2. Lookup in current module's import table
	if (const auto it_imp = module_imports.find(current_module); it_imp != module_imports.end()) {
		const auto& imports = it_imp->second;
		if (const auto it = imports.find(raw_name); it != imports.end()) {
			const std::string& target = it->second;
			if (const auto it_sym = symbol_table.find(target); it_sym != symbol_table.end()) {
				const auto& sym = it_sym->second;
				if (!sym.is_pub && sym.module_name != current_module) {
					logger.error(line, col, std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name + "' is private and cannot be accessed from outside");
				}
				return target;
			}
		}
	}

	// 3. Lookup in wildcard imports
	if (const auto it_wc = module_wildcards.find(current_module); it_wc != module_wildcards.end()) {
		for (const auto& w_mod : it_wc->second) {
			const std::string candidate = w_mod + "." + name;
			if (const auto it_sym = symbol_table.find(candidate); it_sym != symbol_table.end()) {
				const auto& sym = it_sym->second;
				if (!sym.is_pub && sym.module_name != current_module) {
					logger.error(line, col, std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name + "' is private and cannot be accessed from outside");
				}
				return candidate;
			}
		}
	}

	// 4. Direct lookup (global / extern)
	if (const auto it_sym = symbol_table.find(raw_name); it_sym != symbol_table.end()) {
		const auto& sym = it_sym->second;
		if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
			logger.error(line, col, std::string(entity_type_name) + " '" + name + "' in module '" + sym.module_name + "' is private and cannot be accessed from outside");
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

std::string Analyzer::resolve_enum_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(enums, raw_name, "Enum", line, col);
}

std::string Analyzer::resolve_const_name(const std::string_view raw_name, const size_t line, const size_t col) {
	return resolve_symbol_helper(constants, raw_name, "Constant", line, col);
}

void Analyzer::pass0_index_modules(const Program *program) {
	decl_modules.clear();
	module_imports.clear();
	module_wildcards.clear();
	known_modules.clear();

	std::string active_mod = "";
	for (const auto &decl : program->declarations) {
		if (isa<ModuleDecl>(decl.get())) {
			active_mod = join_path(as<ModuleDecl>(decl.get())->path);
			known_modules.insert(active_mod);
		} else if (isa<UseDecl>(decl.get())) {
			const auto *u = as<UseDecl>(decl.get());
			decl_modules[u] = active_mod;
			std::string full_path = join_path(u->path);
			if (u->is_wildcard) {
				module_wildcards[active_mod].push_back(full_path);
			} else {
				std::string sym = std::string(u->symbol_name);
				std::string alias = u->alias.empty() ? sym : std::string(u->alias);
				std::string target = full_path + "." + sym;
				module_imports[active_mod][alias] = target;
			}
		} else {
			decl_modules[decl.get()] = active_mod;
		}
	}
}

void Analyzer::validate_use_declarations(const Program *program) {
	for (const auto &decl : program->declarations) {
		if (isa<UseDecl>(decl.get())) {
			const auto *u = as<UseDecl>(decl.get());
			std::string mod = get_decl_module(u);
			std::string full_path = join_path(u->path);

			if (u->is_wildcard) {
				if (!known_modules.contains(full_path)) {
					bool mod_found = false;
					for (const auto &[name, sym] : functions) {
						if (sym.module_name == full_path) { mod_found = true; break; }
					}
					if (!mod_found) {
						for (const auto &[name, sym] : structs) {
							if (sym.module_name == full_path) { mod_found = true; break; }
						}
					}
					if (!mod_found) {
						for (const auto &[name, sym] : enums) {
							if (sym.module_name == full_path) { mod_found = true; break; }
						}
					}
					if (!mod_found) {
						for (const auto &[name, sym] : constants) {
							if (sym.module_name == full_path) { mod_found = true; break; }
						}
					}
					if (!mod_found) {
						logger.error(u->line, u->col, "Module '" + full_path + "' not found");
					}
				}
			} else {
				std::string sym_name = std::string(u->symbol_name);
				std::string target = full_path + "." + sym_name;

				bool found = false;
				bool is_pub = false;

				if (functions.contains(target)) {
					found = true;
					is_pub = functions.at(target).is_pub;
				} else if (structs.contains(target)) {
					found = true;
					is_pub = structs.at(target).is_pub;
				} else if (enums.contains(target)) {
					found = true;
					is_pub = enums.at(target).is_pub;
				} else if (constants.contains(target)) {
					found = true;
					is_pub = constants.at(target).is_pub;
				}

				if (!found) {
					logger.error(u->line, u->col, "Symbol '" + sym_name + "' not found in module '" + full_path + "'");
				} else if (!is_pub && full_path != mod) {
					logger.error(u->line, u->col, "Symbol '" + sym_name + "' in module '" + full_path + "' is private and cannot be imported");
				}
			}
		}
	}
}
