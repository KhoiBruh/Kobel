module;

#include <string>
#include <string_view>
#include <vector>

export module semantic.symbol;

import token;
import semantic;
import ast;

export inline std::string to_llvm_name(std::string_view name) {
	if (name == "main") return "main";
	std::string res;
	res.reserve(name.size());
	for (const char c: name) {
		if (c == '.' || c == '<' || c == '>' || c == ',') {
			if (res.empty() || res.back() != '_') res += '_';
		} else if (c == ' ') {
			continue;
		} else {
			res += c;
		}
	}
	if (!res.empty() && res.back() == '_') res.pop_back();
	return res;
}

export using ::parse_kobel_int;

export struct VarSymbol {
	std::string name;
	Semantic type;
	bool is_mut = false;
	size_t line = 0;
	size_t col = 0;
};

export struct FnSymbol {
	std::string name;
	std::vector<Semantic> param_types;
	std::vector<std::string> param_names;
	Semantic return_type;
	bool is_pub = false;
	std::string module_name;
	size_t line = 0;
	size_t col = 0;
};

export struct StructSymbol {
	std::string name;
	StringMap<Semantic> field_types;
	std::vector<std::string> field_order;
	StringMap<bool> field_pub;
	StringMap<FnSymbol> methods;
	std::vector<const FnDecl *> method_decls;
	std::vector<std::string> traits;
	bool is_pub = false;
	std::string module_name;
	size_t line = 0;
	size_t col = 0;
};

export struct TraitSymbol {
	std::string name;
	bool is_pub = false;
	std::string module_name;
	std::vector<std::string> base_traits;
	StringMap<const FnDecl *> required_methods;
	StringMap<const FnDecl *> default_methods;
	size_t line = 0;
	size_t col = 0;
};

export struct ConstSymbol {
	std::string name;
	Semantic type;
	bool is_pub = false;
	std::string module_name;
	size_t line = 0;
	size_t col = 0;
};

export struct EnumSymbol {
	std::string name;
	Semantic underlying_type;
	StringMap<int64_t> member_values;
	bool is_pub = false;
	std::string module_name;
	size_t line = 0;
	size_t col = 0;
};

export struct Scope {
	StringMap<VarSymbol> variables;
};
