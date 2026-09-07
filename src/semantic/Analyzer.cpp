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

	StringMap<FnSymbol> functions;
	StringMap<StructSymbol> structs;
	StringMap<EnumSymbol> enums;
	StringMap<ConstSymbol> constants;
	std::unordered_map<const Expr *, Semantic> expr_types;

	std::vector<Scope> scopes;
	std::optional<Semantic> current_function_return_type;
	int loop_depth = 0;

	std::string current_module;
	std::unordered_map<const Decl*, std::string> decl_modules;
	StringMap<StringMap<std::string>> module_imports;
	StringMap<std::vector<std::string>> module_wildcards;
	StringSet known_modules;
	std::unordered_map<const Expr*, std::string> resolved_symbols;
	std::unordered_map<const Expr*, Semantic> resolved_type_sizes;

		std::vector<std::unique_ptr<Type>> interned_types;

	Semantic make_primitive(SemaType k) {
		for (const auto& t : interned_types) {
			if (t->kind == k && k != SemaType::POINTER && k != SemaType::STRUCT && k != SemaType::ENUM && k != SemaType::ARRAY) {
				return t.get();
			}
		}
		auto t = std::make_unique<Type>();
		t->kind = k;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_pointer(Semantic target, bool mut = false) {
		for (const auto& t : interned_types) {
			if (t->kind == SemaType::POINTER && t->pointee == target && t->is_mut_pointer == mut)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::POINTER;
		t->pointee = target;
		t->is_mut_pointer = mut;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_struct(const std::string_view name) {
		for (const auto& t : interned_types) {
			if (t->kind == SemaType::STRUCT && t->struct_name == name)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::STRUCT;
		t->struct_name = std::string(name);
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_enum(const std::string_view name, Semantic under) {
		for (const auto& t : interned_types) {
			if (t->kind == SemaType::ENUM && t->enum_name == name && t->underlying_type == under)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::ENUM;
		t->enum_name = std::string(name);
		t->underlying_type = under;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_array(Semantic elem, size_t sz = 0) {
		for (const auto& t : interned_types) {
			if (t->kind == SemaType::ARRAY && t->element_type == elem && t->array_size == sz)
				return t.get();
		}
		auto t = std::make_unique<Type>();
		t->kind = SemaType::ARRAY;
		t->element_type = elem;
		t->array_size = sz;
		interned_types.push_back(std::move(t));
		return interned_types.back().get();
	}

	Semantic make_void() { return make_primitive(SemaType::VOID); }
	Semantic make_null() { return make_primitive(SemaType::NULL_TYPE); }
	Semantic make_error() { return make_primitive(SemaType::ERROR_TYPE); }
	Semantic make_str() { return make_primitive(SemaType::STR); }
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
		for (auto &[variables] : std::views::reverse(scopes)) {
			auto found = variables.find(name);
			if (found != variables.end()) return &found->second;
		}
		return nullptr;
	}

	// Module & Symbol resolution helpers (AnalyzerModule.cpp)
	std::string get_decl_module(const Decl* decl) const;

	template <typename TSymbol>
	std::string resolve_symbol_helper(
		const StringMap<TSymbol>& symbol_table,
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
	Semantic resolve_type_by_name(std::string_view name, size_t line = 0, size_t col = 0);

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
	Semantic get_expr_type(const Expr *expr);

	// Overall Analysis Driver
	void analyze(const Program *program) {
		pass0_index_modules(program);
		pass1_register_declarations(program);
		validate_use_declarations(program);
		pass2_check_declarations(program);
	}
};






