module;

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

export module semantic.symbol;

import semantic;

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
	size_t line = 0;
	size_t col = 0;
};

export struct StructSymbol {
	std::string name;
	std::unordered_map<std::string, Semantic> field_types;
	std::vector<std::string> field_order;
	size_t line = 0;
	size_t col = 0;
};

export struct ConstSymbol {
	std::string name;
	Semantic type;
	size_t line = 0;
	size_t col = 0;
};

export struct EnumSymbol {
	std::string name;
	Semantic underlying_type;
	std::unordered_map<std::string, int64_t> member_values;
	size_t line = 0;
	size_t col = 0;
};

export struct Scope {
	std::unordered_map<std::string, VarSymbol> variables;
};
