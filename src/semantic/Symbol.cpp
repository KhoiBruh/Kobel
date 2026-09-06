module;

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
	std::unordered_map<std::string, Semantic> field_types;
	std::vector<std::string> field_order;
	std::unordered_map<std::string, FnSymbol> methods;
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
	std::unordered_map<std::string, int64_t> member_values;
	bool is_pub = false;
	std::string module_name = "";
	size_t line = 0;
	size_t col = 0;
};

export struct Scope {
	std::unordered_map<std::string, VarSymbol> variables;
};
