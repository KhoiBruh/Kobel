module;

#include <memory>
#include <optional>
#include <ranges>
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
	DiagnosticEngine &logger;

	std::unordered_map<std::string, FnSymbol> functions;
	std::unordered_map<std::string, StructSymbol> structs;
	std::unordered_map<std::string, EnumSymbol> enums;
	std::unordered_map<std::string, ConstSymbol> constants;
	std::unordered_map<const Expr *, Semantic> expr_types;

	std::vector<Scope> scopes;
	std::optional<Semantic> current_function_return_type;
	int loop_depth = 0;

	explicit Analyzer(DiagnosticEngine &log) : logger(log) {
	}

	// Scope helpers
	void enter_scope() {
		scopes.emplace_back();
	}

	void exit_scope() {
		if (!scopes.empty()) scopes.pop_back();
	}

	Scope &current_scope() {
		return scopes.back();
	}

	VarSymbol *lookup_variable(const std::string_view name) {
		for (auto &scope: std::views::reverse(scopes)) {
			auto found = scope.variables.find(std::string(name));
			if (found != scope.variables.end()) return &found->second;
		}
		return nullptr;
	}

	std::string current_module;
	std::unordered_map<const Decl*, std::string> decl_modules;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> module_imports;
	std::unordered_map<std::string, std::vector<std::string>> module_wildcards;
	std::unordered_map<const Expr*, std::string> resolved_symbols;

	std::string get_decl_module(const Decl* decl) const {
		auto it = decl_modules.find(decl);
		if (it != decl_modules.end()) return it->second;
		return "";
	}

	static std::string join_path(const std::vector<std::string_view>& path) {
		std::string res;
		for (size_t i = 0; i < path.size(); ++i) {
			if (i > 0) res += ".";
			res += path[i];
		}
		return res;
	}

	std::string resolve_function_name(const std::string_view raw_name, const size_t line = 0, const size_t col = 0) {
		std::string name = std::string(raw_name);

		// 1. Trong module hiện tại
		if (!current_module.empty()) {
			std::string local_qualified = current_module + "." + name;
			if (functions.contains(local_qualified)) return local_qualified;
		}

		// 2. Tra cứu trong bảng import của module hiện tại
		if (module_imports.contains(current_module)) {
			const auto& imports = module_imports.at(current_module);
			auto it = imports.find(name);
			if (it != imports.end()) {
				const std::string& target = it->second;
				if (functions.contains(target)) {
					const auto& sym = functions.at(target);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Hàm '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return target;
				}
			}
		}

		// 3. Tra cứu wildcard
		if (module_wildcards.contains(current_module)) {
			for (const auto& w_mod : module_wildcards.at(current_module)) {
				std::string candidate = w_mod + "." + name;
				if (functions.contains(candidate)) {
					const auto& sym = functions.at(candidate);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Hàm '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return candidate;
				}
			}
		}

		// 4. Tra cứu trực tiếp (toàn cục / extern)
		if (functions.contains(name)) {
			const auto& sym = functions.at(name);
			if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
				logger.error(line, col, "Hàm '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
			}
			return name;
		}

		return "";
	}

	std::string resolve_struct_name(const std::string_view raw_name, const size_t line = 0, const size_t col = 0) {
		std::string name = std::string(raw_name);

		// 1. Trong module hiện tại
		if (!current_module.empty()) {
			std::string local_qualified = current_module + "." + name;
			if (structs.contains(local_qualified)) return local_qualified;
		}

		// 2. Tra cứu import
		if (module_imports.contains(current_module)) {
			const auto& imports = module_imports.at(current_module);
			auto it = imports.find(name);
			if (it != imports.end()) {
				const std::string& target = it->second;
				if (structs.contains(target)) {
					const auto& sym = structs.at(target);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Struct '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return target;
				}
			}
		}

		// 3. Tra cứu wildcard
		if (module_wildcards.contains(current_module)) {
			for (const auto& w_mod : module_wildcards.at(current_module)) {
				std::string candidate = w_mod + "." + name;
				if (structs.contains(candidate)) {
					const auto& sym = structs.at(candidate);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Struct '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return candidate;
				}
			}
		}

		// 4. Tra cứu trực tiếp
		if (structs.contains(name)) {
			const auto& sym = structs.at(name);
			if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
				logger.error(line, col, "Struct '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
			}
			return name;
		}

		return "";
	}

	std::string resolve_enum_name(const std::string_view raw_name, const size_t line = 0, const size_t col = 0) {
		std::string name = std::string(raw_name);

		// 1. Trong module hiện tại
		if (!current_module.empty()) {
			std::string local_qualified = current_module + "." + name;
			if (enums.contains(local_qualified)) return local_qualified;
		}

		// 2. Tra cứu import
		if (module_imports.contains(current_module)) {
			const auto& imports = module_imports.at(current_module);
			auto it = imports.find(name);
			if (it != imports.end()) {
				const std::string& target = it->second;
				if (enums.contains(target)) {
					const auto& sym = enums.at(target);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Enum '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return target;
				}
			}
		}

		// 3. Tra cứu wildcard
		if (module_wildcards.contains(current_module)) {
			for (const auto& w_mod : module_wildcards.at(current_module)) {
				std::string candidate = w_mod + "." + name;
				if (enums.contains(candidate)) {
					const auto& sym = enums.at(candidate);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Enum '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return candidate;
				}
			}
		}

		// 4. Tra cứu trực tiếp
		if (enums.contains(name)) {
			const auto& sym = enums.at(name);
			if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
				logger.error(line, col, "Enum '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
			}
			return name;
		}

		return "";
	}

	std::string resolve_const_name(const std::string_view raw_name, const size_t line = 0, const size_t col = 0) {
		std::string name = std::string(raw_name);

		// 1. Trong module hiện tại
		if (!current_module.empty()) {
			std::string local_qualified = current_module + "." + name;
			if (constants.contains(local_qualified)) return local_qualified;
		}

		// 2. Tra cứu import
		if (module_imports.contains(current_module)) {
			const auto& imports = module_imports.at(current_module);
			auto it = imports.find(name);
			if (it != imports.end()) {
				const std::string& target = it->second;
				if (constants.contains(target)) {
					const auto& sym = constants.at(target);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Hằng số '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return target;
				}
			}
		}

		// 3. Tra cứu wildcard
		if (module_wildcards.contains(current_module)) {
			for (const auto& w_mod : module_wildcards.at(current_module)) {
				std::string candidate = w_mod + "." + name;
				if (constants.contains(candidate)) {
					const auto& sym = constants.at(candidate);
					if (!sym.is_pub && sym.module_name != current_module) {
						logger.error(line, col, "Hằng số '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
					}
					return candidate;
				}
			}
		}

		// 4. Tra cứu trực tiếp
		if (constants.contains(name)) {
			const auto& sym = constants.at(name);
			if (!sym.module_name.empty() && sym.module_name != current_module && !sym.is_pub) {
				logger.error(line, col, "Hằng số '" + name + "' trong module '" + sym.module_name + "' là private và không thể truy cập từ bên ngoài");
			}
			return name;
		}

		return "";
	}

	// ========================================================================
	// Ánh xạ TypeNode (AST) -> Semantic (Semantic)
	// ========================================================================

	Semantic resolve_type(const TypeNode *node) {
		if (!node) return Semantic::make_primitive(SemaType::VOID);

		if (isa<NamedType>(node)) {
			const auto *named = as<NamedType>(node);
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
			std::string resolved_st = resolve_struct_name(name, node->line, node->col);
			if (!resolved_st.empty()) {
				return Semantic::make_struct(resolved_st);
			}

			// Kiểm tra enum đã khai báo
			std::string resolved_enum = resolve_enum_name(name, node->line, node->col);
			if (!resolved_enum.empty()) {
				return Semantic::make_enum(resolved_enum, enums.at(resolved_enum).underlying_type);
			}

			logger.error(node->line, node->col, "Không tìm thấy kiểu dữ liệu '" + std::string(name) + "'");
			return Semantic::make_error();
		}

		if (isa<PointerType>(node)) {
			const auto *ptr = as<PointerType>(node);
			auto pointee_type = resolve_type(ptr->pointee.get());
			return Semantic::make_pointer(std::move(pointee_type), ptr->is_mut);
		}

		if (isa<ArrayType>(node)) {
			const auto *arr = as<ArrayType>(node);
			auto elem_type = resolve_type(arr->element_type.get());
			return Semantic::make_array(std::move(elem_type), arr->size);
		}

		logger.error(node->line, node->col, "Kiểu dữ liệu cú pháp không hợp lệ");
		return Semantic::make_error();
	}

	// ========================================================================
	// Pass 0: Đăng ký Module và Import
	// ========================================================================

	void pass0_index_modules(const Program *program) {
		decl_modules.clear();
		module_imports.clear();
		module_wildcards.clear();

		std::string active_mod = "";
		for (const auto &decl : program->declarations) {
			if (isa<ModuleDecl>(decl.get())) {
				active_mod = join_path(as<ModuleDecl>(decl.get())->path);
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

	// ========================================================================
	// Pass 1: Đăng ký Khai báo Top-Level (Hoisting)
	// ========================================================================

	void pass1_register_declarations(const Program *program) {
		// 1. Đăng ký các Struct
		for (const auto &decl: program->declarations) {
			if (isa<StructDecl>(decl.get())) {
				const auto *st = as<StructDecl>(decl.get());
				std::string mod = get_decl_module(st);
				std::string qual_name = mod.empty() ? std::string(st->name) : mod + "." + std::string(st->name);

				if (structs.contains(qual_name)) {
					logger.error(st->line, st->col, "Trùng lặp khai báo struct '" + std::string(st->name) + "'");
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

		// Sau khi có tên struct, đăng ký các trường và phương thức của struct
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
							st->line, st->col, "Trùng lặp trường '" + f_name + "' trong struct '" + sym.name + "'"
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
							"Trùng lặp phương thức hoặc trường '" + m_name + "' trong struct '" + sym.name + "'"
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

		// 2. Đăng ký các Enum
		for (const auto &decl: program->declarations) {
			if (isa<EnumDecl>(decl.get())) {
				const auto *e = as<EnumDecl>(decl.get());
				std::string mod = get_decl_module(e);
				std::string qual_name = mod.empty() ? std::string(e->name) : mod + "." + std::string(e->name);

				if (enums.contains(qual_name) || structs.contains(qual_name)) {
					logger.error(e->line, e->col, "Trùng lặp tên kiểu '" + std::string(e->name) + "'");
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
					logger.error(e->line, e->col, "Kiểu cơ sở của enum bắt buộc phải là số nguyên");
					sym.underlying_type = Semantic::make_primitive(SemaType::I32);
				}

				int64_t next_value = 0;
				for (const auto &m: e->members) {
					auto m_name = std::string(m.name);
					if (sym.member_values.contains(m_name)) {
						logger.error(m.line, m.col, "Trùng lặp thành viên '" + m_name + "' trong enum '" + std::string(e->name) + "'");
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

				enums[qual_name] = sym;
				if (!mod.empty()) {
					enums[to_llvm_name(qual_name)] = sym;
				}
			}
		}

		// 3. Đăng ký các Hằng số
		for (const auto &decl: program->declarations) {
			if (isa<ConstDecl>(decl.get())) {
				const auto *c = as<ConstDecl>(decl.get());
				std::string mod = get_decl_module(c);
				std::string qual_name = mod.empty() ? std::string(c->name) : mod + "." + std::string(c->name);

				if (constants.contains(qual_name)) {
					logger.error(c->line, c->col, "Trùng lặp khai báo hằng số '" + std::string(c->name) + "'");
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

		// 4. Đăng ký Hàm (bao gồm cả khối extern)
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

	void register_function(const FnDecl *fn, const std::string &mod) {
		const auto raw_name = std::string(fn->name);
		const std::string qual_name = (mod.empty() || raw_name == "main") ? raw_name : mod + "." + raw_name;

		if (functions.contains(qual_name)) {
			logger.error(fn->line, fn->col, "Trùng lặp khai báo hàm '" + raw_name + "'");
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

	// ========================================================================
	// Thẩm định Câu lệnh Use & Visibility
	// ========================================================================

	void validate_use_declarations(const Program *program) {
		for (const auto &decl : program->declarations) {
			if (isa<UseDecl>(decl.get())) {
				const auto *u = as<UseDecl>(decl.get());
				std::string mod = get_decl_module(u);
				std::string full_path = join_path(u->path);

				if (u->is_wildcard) {
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
						logger.error(u->line, u->col, "Không tìm thấy module '" + full_path + "'");
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
						logger.error(u->line, u->col, "Không tìm thấy symbol '" + sym_name + "' trong module '" + full_path + "'");
					} else if (!is_pub && full_path != mod) {
						logger.error(u->line, u->col, "Symbol '" + sym_name + "' trong module '" + full_path + "' là private và không thể import");
					}
				}
			}
		}
	}

	// ========================================================================
	// Pass 2: Kiểm tra Ngữ nghĩa & Kiểu Thân Hàm (Type Checking)
	// ========================================================================

	void pass2_check_declarations(const Program *program) {
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
				if (
					auto expected_type = constants[c_qual].type;
					!expected_type.can_assign_from(val_type)
				)
					logger.error(
						c->line, c->col, "Giá trị khởi tạo hằng số không khớp kiểu: mong đợi '" +
						                 expected_type.to_string() + "', gặp '" + val_type.to_string() + "'"
					);
			}
		}
	}

	void check_function(const FnDecl *fn, const std::string &fn_lookup_name) {
		if (!fn->body) return; // Hàm prototype không có thân

		const auto &sym = functions[fn_lookup_name];
		current_function_return_type = sym.return_type;

		enter_scope(); // Scope mức hàm

		// Đăng ký tham số hàm
		for (size_t i = 0; i < sym.param_names.size(); ++i) {
			VarSymbol p_sym;
			p_sym.name = sym.param_names[i];
			p_sym.type = sym.param_types[i];
			p_sym.is_mut = (i < fn->params.size()) ? fn->params[i].is_mut : false;
			p_sym.line = fn->line;
			p_sym.col = fn->col;
			current_scope().variables[p_sym.name] = p_sym;
		}

		// Duyệt các câu lệnh trong thân hàm
		for (const auto &stmt: fn->body->statements) {
			analyze_stmt(stmt.get());
		}

		exit_scope();
		current_function_return_type.reset();
	}

	// ========================================================================
	// Thẩm định Câu lệnh (Statements)
	// ========================================================================

	void analyze_stmt(const Stmt *stmt) {
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

	// ========================================================================
	// Thẩm định Biểu thức (Expressions) & Trả về Kiểu Ngữ nghĩa
	// ========================================================================

	Semantic compute_expr_type(const Expr *expr) {
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

	Semantic analyze_expr(const Expr *expr) {
		if (!expr) return Semantic::make_error();
		auto ty = compute_expr_type(expr);
		expr_types[expr] = ty;
		return ty;
	}

	Semantic get_expr_type(const Expr *expr) const {
		if (!expr) return Semantic::make_error();
		auto it = expr_types.find(expr);
		if (it != expr_types.end()) return it->second;
		return Semantic::make_error();
	}

	// ========================================================================
	// Hàm Phân tích Tổng thể
	// ========================================================================

	void analyze(const Program *program) {
		pass0_index_modules(program);
		pass1_register_declarations(program);
		validate_use_declarations(program);
		pass2_check_declarations(program);
	}
};
