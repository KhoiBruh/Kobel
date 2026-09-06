module;

#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module semantic.symbol;

import semantic;

export inline std::string to_llvm_name(std::string_view name) {
	if (name == "main") return "main";
	std::string res;
	res.reserve(name.size());
	for (char c : name) {
		if (c == '.') res += '_';
		else res += c;
	}
	return res;
}

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
	std::string module_name = "";
	size_t line = 0;
	size_t col = 0;
};

export struct StructSymbol {
	std::string name;
	StringMap<Semantic> field_types;
	std::vector<std::string> field_order;
	StringMap<FnSymbol> methods;
	bool is_pub = false;
	std::string module_name = "";
	size_t line = 0;
	size_t col = 0;
};

export struct ConstSymbol {
	std::string name;
	Semantic type;
	bool is_pub = false;
	std::string module_name = "";
	size_t line = 0;
	size_t col = 0;
};

export struct EnumSymbol {
	std::string name;
	Semantic underlying_type;
	StringMap<int64_t> member_values;
	bool is_pub = false;
	std::string module_name = "";
	size_t line = 0;
	size_t col = 0;
};

export struct Scope {
	StringMap<VarSymbol> variables;
};
