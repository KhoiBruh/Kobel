module;

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

	std::unordered_map<std::string, FnSymbol> functions;
	std::unordered_map<std::string, StructSymbol> structs;
	std::unordered_map<std::string, EnumSymbol> enums;
	std::unordered_map<std::string, ConstSymbol> constants;
	std::unordered_map<const Expr *, Semantic> expr_types;

	std::vector<Scope> scopes;
	std::optional<Semantic> current_function_return_type;
	int loop_depth = 0;

	std::string current_module;
	std::unordered_map<const Decl*, std::string> decl_modules;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> module_imports;
	std::unordered_map<std::string, std::vector<std::string>> module_wildcards;
	std::unordered_set<std::string> known_modules;
	std::unordered_map<const Expr*, std::string> resolved_symbols;

	explicit Analyzer(DiagnosticEngine &log) : logger(log) {}

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
		for (auto &scope : std::views::reverse(scopes)) {
			auto found = scope.variables.find(std::string(name));
			if (found != scope.variables.end()) return &found->second;
		}
		return nullptr;
	}

	// Module & Symbol resolution helpers (AnalyzerModule.cpp)
	std::string get_decl_module(const Decl* decl) const;
	static std::string join_path(const std::vector<std::string_view>& path);

	template <typename TSymbol>
	std::string resolve_symbol_helper(
		const std::unordered_map<std::string, TSymbol>& symbol_table,
		std::string_view raw_name,
		std::string_view entity_type_name,
		size_t line, size_t col
	);

	std::string resolve_function_name(std::string_view raw_name, size_t line = 0, size_t col = 0);
	std::string resolve_struct_name(std::string_view raw_name, size_t line = 0, size_t col = 0);
	std::string resolve_enum_name(std::string_view raw_name, size_t line = 0, size_t col = 0);
	std::string resolve_const_name(std::string_view raw_name, size_t line = 0, size_t col = 0);

	// Type mapping (AnalyzerType.cpp)
	Semantic resolve_type(const TypeNode *node);

	// Declarations & Passes (AnalyzerModule.cpp & AnalyzerDecl.cpp)
	void pass0_index_modules(const Program *program);
	void pass1_register_declarations(const Program *program);
	void register_function(const FnDecl *fn, const std::string &mod);
	void validate_use_declarations(const Program *program);
	void pass2_check_declarations(const Program *program);
	void check_function(const FnDecl *fn, const std::string &fn_lookup_name);
	static bool has_definite_return(const Stmt *stmt);

	// Statements (AnalyzerStmt.cpp)
	void analyze_stmt(const Stmt *stmt);

	// Expressions (AnalyzerExpr.cpp)
	Semantic compute_expr_type(const Expr *expr);
	Semantic analyze_expr(const Expr *expr);
	Semantic get_expr_type(const Expr *expr) const;

	// Overall Analysis Driver
	void analyze(const Program *program) {
		pass0_index_modules(program);
		pass1_register_declarations(program);
		validate_use_declarations(program);
		pass2_check_declarations(program);
	}
};
