module;

#include <array>
#include <memory>
#include <optional>
#include <ranges>
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
	StringMap<StringMap<Semantic>> instantiated_type_maps;
	StringMap<Semantic> active_type_substitutions;
	StringMap<const FnDecl *> generic_functions;
	std::vector<std::string> instantiated_function_order;
	StringMap<const FnDecl *> instantiated_fn_decls;
	StringMap<StringMap<Semantic>> instantiated_fn_type_maps;
	StringMap<EnumSymbol> enums;
	StringMap<ConstSymbol> constants;
	StringMap<TraitSymbol> traits;
	StringMap<std::vector<std::string>> struct_traits;
	struct InheritedTraitMethod {
		std::string method_name;
		const FnDecl *fn_decl;
	};
	StringMap<std::vector<InheritedTraitMethod>> struct_default_methods;
	StringMap<StructDecl *> all_struct_decls;
	std::vector<std::vector<FnDecl *>> merged_methods_storage;
	std::vector<std::vector<std::string_view>> merged_traits_storage;
	std::vector<std::string> resolved_trait_names_storage;
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

	Semantic instantiate_struct(const StructDecl *generic_st, const std::string &instantiated_name, const std::vector<Semantic> &type_args, size_t line, size_t col);

	std::string resolve_generic_function_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	Semantic instantiate_function(const FnDecl *generic_fn, const std::string &instantiated_name, const std::vector<Semantic> &type_args, size_t line, size_t col);

	// Declarations & Passes (AnalyzerModule.cpp & AnalyzerDecl.cpp)
	void pass0_index_modules(const Program *program);

	void pass1_register_declarations(const Program *program);

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

	void validate_use_declarations(const Program *program);

	void pass2_check_declarations(const Program *program);

	void check_function(const FnDecl *fn, const std::string &fn_lookup_name);

	static bool has_definite_return(const Stmt *stmt);

	// Statements (AnalyzerStmt.cpp)
	void analyze_stmt(const Stmt *stmt);

	void analyze_when_stmt(const WhenStmt *stmt);

	// Expressions (AnalyzerExpr.cpp)
	Semantic compute_expr_type(const Expr *expr);

	Semantic analyze_expr(const Expr *expr);

	Semantic get_expr_type(const Expr *expr);

	Semantic analyze_if_expr(const IfExpr *expr);

	Semantic analyze_when_expr(const WhenExpr *expr);

	// Overall Analysis Driver
	void analyze(const Program *program) {
		pass0_index_modules(program);
		pass1_register_declarations(program);
		validate_use_declarations(program);
		pass2_check_declarations(program);
	}
};
