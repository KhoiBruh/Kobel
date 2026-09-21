module;

#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module semantic.analyzer;

export import ast;
export import token;
export import logger;
export import semantic;
export import semantic.symbol;

export struct Analyzer {
	DiagnosticEngine &logger;

	StringMap<FnSymbol> functions;
	StringMap<StructSymbol> structs;
	StringMap<const StructDecl *> generic_structs;
	std::vector<std::string> instantiated_struct_order;
	StringMap<StringMap<Semantic> > instantiated_type_maps;
	StringMap<Semantic> active_type_substitutions;
	StringMap<const FnDecl *> generic_functions;
	std::vector<std::string> instantiated_function_order;
	StringMap<const FnDecl *> instantiated_fn_decls;
	StringMap<StringMap<Semantic> > instantiated_fn_type_maps;
	StringMap<EnumSymbol> enums;
	StringMap<ConstSymbol> constants;
	StringMap<TraitSymbol> traits;
	StringMap<std::vector<std::string> > struct_traits;

	struct InheritedTraitMethod {
		std::string method_name;
		const FnDecl *fn_decl;
	};

	StringMap<std::vector<InheritedTraitMethod> > struct_default_methods;
	StringMap<std::vector<const FnDecl *> > generic_struct_methods;
	StringMap<std::vector<std::string> > generic_struct_traits;
	std::unordered_map<const Expr *, Semantic> expr_types;

	std::vector<Scope> scopes;
	std::optional<Semantic> current_function_return_type;
	Semantic current_self_type = nullptr;
	int loop_depth = 0;

	std::string current_module;
	std::unordered_map<const Decl *, std::string> decl_modules;
	StringMap<StringMap<std::string> > module_imports;
	StringMap<StringSet> ambiguous_imports;
	StringMap<std::vector<std::string> > module_wildcards;
	StringSet known_modules;
	std::unordered_map<const Expr *, std::string> resolved_symbols;
	std::unordered_map<const Expr *, Semantic> resolved_type_sizes;

	std::string current_function_name;
	std::unordered_map<std::string, std::unordered_map<const Expr *, Semantic>> fn_expr_types;
	std::unordered_map<std::string, std::unordered_map<const Expr *, std::string>> fn_resolved_symbols;
	std::unordered_map<std::string, std::unordered_map<const Expr *, Semantic>> fn_resolved_type_sizes;

	void record_expr_type(const Expr *expr, Semantic ty) {
		expr_types[expr] = ty;
		if (!current_function_name.empty()) {
			fn_expr_types[current_function_name][expr] = ty;
			fn_expr_types[to_llvm_name(current_function_name)][expr] = ty;
		}
	}

	void record_resolved_symbol(const Expr *expr, const std::string &sym) {
		resolved_symbols[expr] = sym;
		if (!current_function_name.empty()) {
			fn_resolved_symbols[current_function_name][expr] = sym;
			fn_resolved_symbols[to_llvm_name(current_function_name)][expr] = sym;
		}
	}

	void record_resolved_type_size(const Expr *expr, Semantic sem) {
		resolved_type_sizes[expr] = sem;
		if (!current_function_name.empty()) {
			fn_resolved_type_sizes[current_function_name][expr] = sem;
			fn_resolved_type_sizes[to_llvm_name(current_function_name)][expr] = sem;
		}
	}

	std::optional<std::string> get_resolved_symbol(const Expr *expr, const std::string &fn_name = "") const {
		if (!expr) return std::nullopt;
		if (!fn_name.empty()) {
			auto it_fn = fn_resolved_symbols.find(fn_name);
			if (it_fn != fn_resolved_symbols.end()) {
				auto it_e = it_fn->second.find(expr);
				if (it_e != it_fn->second.end()) return it_e->second;
			}
		} else if (!current_function_name.empty()) {
			auto it_fn = fn_resolved_symbols.find(current_function_name);
			if (it_fn != fn_resolved_symbols.end()) {
				auto it_e = it_fn->second.find(expr);
				if (it_e != it_fn->second.end()) return it_e->second;
			}
		}
		auto it = resolved_symbols.find(expr);
		if (it != resolved_symbols.end()) return it->second;
		return std::nullopt;
	}

	Semantic get_resolved_type_size(const Expr *expr, const std::string &fn_name = "") const {
		if (!expr) return nullptr;
		if (!fn_name.empty()) {
			auto it_fn = fn_resolved_type_sizes.find(fn_name);
			if (it_fn != fn_resolved_type_sizes.end()) {
				auto it_e = it_fn->second.find(expr);
				if (it_e != it_fn->second.end()) return it_e->second;
			}
		} else if (!current_function_name.empty()) {
			auto it_fn = fn_resolved_type_sizes.find(current_function_name);
			if (it_fn != fn_resolved_type_sizes.end()) {
				auto it_e = it_fn->second.find(expr);
				if (it_e != it_fn->second.end()) return it_e->second;
			}
		}
		auto it = resolved_type_sizes.find(expr);
		if (it != resolved_type_sizes.end()) return it->second;
		return nullptr;
	}

	TypeContext type_ctx;

	Semantic make_primitive(SemaType k) const { return type_ctx.make_primitive(k); }
	Semantic make_pointer(Semantic target, bool mut = false) { return type_ctx.make_pointer(target, mut); }
	Semantic make_struct(std::string_view name) { return type_ctx.make_struct(name); }
	Semantic make_enum(std::string_view name, Semantic under) { return type_ctx.make_enum(name, under); }
	Semantic make_array(Semantic elem, size_t sz = 0) { return type_ctx.make_array(elem, sz); }

	Semantic make_void() const { return type_ctx.make_void(); }
	Semantic make_null() const { return type_ctx.make_null(); }
	Semantic make_error() const { return type_ctx.make_error(); }
	Semantic make_str() const { return type_ctx.make_str(); }

	explicit Analyzer(DiagnosticEngine &log) : logger(log) {
	}

	Analyzer(DiagnosticEngine &log, TypeContext ctx) : logger(log), type_ctx(std::move(ctx)) {
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
		for (auto &[variables]: std::views::reverse(scopes)) {
			auto found = variables.find(name);
			if (found != variables.end()) return &found->second;
		}
		return nullptr;
	}

	// Module & Symbol resolution helpers (AnalyzerModule.cpp)
	std::string get_decl_module(const Decl *decl) const;

	template<typename TSymbol>
	std::string resolve_symbol_helper(
		const StringMap<TSymbol> &symbol_table,
		std::string_view raw_name,
		std::string_view entity_type_name,
		size_t line, size_t col
	);

	template<typename TDecl>
	std::string resolve_generic_symbol_helper(
		const StringMap<const TDecl *> &symbol_table,
		std::string_view raw_name,
		size_t line, size_t col
	);

	std::string resolve_function_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	std::string resolve_struct_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	std::string resolve_generic_struct_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	std::string resolve_enum_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	std::string resolve_const_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	std::string resolve_trait_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	bool type_implements_trait(Semantic type, std::string_view trait_name);

	// Type mapping & Generic instantiation (AnalyzerType.cpp)
	Semantic resolve_type(const TypeNode *node);

	Semantic resolve_type_by_name(std::string_view name, size_t line = 0, size_t col = 0);

	Semantic substitute_type(const TypeNode *node, const StringMap<Semantic> &type_map);

	Semantic instantiate_struct(const StructDecl *generic_st, const std::string &instantiated_name,
								const std::vector<Semantic> &type_args, size_t line, size_t col);

	std::string resolve_generic_function_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	Semantic instantiate_function(const FnDecl *generic_fn, const std::string &instantiated_name,
								  const std::vector<Semantic> &type_args, size_t line, size_t col);

	// Declarations & Passes (AnalyzerModule.cpp & AnalyzerDecl.cpp)
	void pass0_index_modules(const Program *program);

	void pass1_register_declarations(const Program *program);

	void register_traits(const Program *program);

	void register_structs(const Program *program);

	void register_enums(const Program *program);

	void register_constants(const Program *program);

	void register_functions(const Program *program);

	void register_function(const FnDecl *fn, const std::string &mod);

	void collect_trait_methods(
		const std::string &trait_name,
		StringMap<const FnDecl *> &out_required,
		StringMap<const FnDecl *> &out_defaults,
		std::vector<std::string> &out_all_traits,
		std::unordered_set<std::string> &visited
	);

	void check_and_apply_struct_traits(
		const StructDecl *st,
		const std::string &qual_name
	);

	const std::vector<const FnDecl *> &get_generic_struct_methods(const std::string &name) const {
		if (auto it = generic_struct_methods.find(name); it != generic_struct_methods.end()) {
			return it->second;
		}
		std::string llvm_name = to_llvm_name(name);
		if (auto it = generic_struct_methods.find(llvm_name); it != generic_struct_methods.end()) {
			return it->second;
		}
		static const std::vector<const FnDecl *> empty;
		return empty;
	}

	const std::vector<std::string> &get_generic_struct_traits(const std::string &name) const {
		if (auto it = generic_struct_traits.find(name); it != generic_struct_traits.end()) {
			return it->second;
		}
		std::string llvm_name = to_llvm_name(name);
		if (auto it = generic_struct_traits.find(llvm_name); it != generic_struct_traits.end()) {
			return it->second;
		}
		static const std::vector<std::string> empty;
		return empty;
	}

	void validate_use_declarations(const Program *program);

	void pass2_check_declarations(const Program *program);

	void check_function(const FnDecl *fn, const std::string &fn_lookup_name);

	static bool has_definite_return(const Stmt *stmt);

	Semantic infer_expression_body_return_type(
		const FnDecl *fn,
		const std::vector<Semantic> &param_types
	);

	// Statements (AnalyzerStmt.cpp)
	void analyze_stmt(const Stmt *stmt);

	void analyze_when_stmt(const WhenStmt *stmt);

	// Expressions (AnalyzerExpr.cpp)
	Semantic compute_expr_type(const Expr *expr);

	Semantic analyze_expr(const Expr *expr);

	Semantic get_expr_type(const Expr *expr, const std::string &fn_name = "");

	Semantic check_and_coerce_arg(
		const Expr *arg,
		Semantic expected_type,
		size_t line,
		size_t col,
		const std::string &desc
	);

	// Contextual typing for unsuffixed integer literals (AnalyzerExpr.cpp)
	Semantic coerce_int_literal_type(const Expr *expr, Semantic expected_type, Semantic actual_type);

	Semantic analyze_literal_expr(const LiteralExpr *lit);

	Semantic analyze_array_literal_expr(const ArrayLiteralExpr *arr_lit);

	Semantic analyze_identifier_expr(const IdentifierExpr *id);

	Semantic analyze_assign_expr(const AssignExpr *a);

	Semantic analyze_binary_expr(const BinaryExpr *b);

	Semantic analyze_unary_expr(const UnaryExpr *u);

	Semantic analyze_cast_expr(const CastExpr *c);

	Semantic analyze_call_expr(const CallExpr *c);

	Semantic analyze_member_expr(const MemberExpr *m);

	Semantic analyze_index_expr(const IndexExpr *idx);

	Semantic analyze_if_expr(const IfExpr *expr);

	Semantic analyze_when_expr(const WhenExpr *expr);

	void validate_when_arm_patterns(std::span<Expr *> patterns, Semantic cond_type);

	// Overall Analysis Driver
	void analyze(const Program *program) {
		pass0_index_modules(program);
		pass1_register_declarations(program);
		validate_use_declarations(program);
		pass2_check_declarations(program);
	}
};
