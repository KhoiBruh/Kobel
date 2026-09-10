module;

#include <cstdlib>
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

export inline int64_t parse_kobel_int(std::string_view raw) {
	std::string s;
	s.reserve(raw.size());
	for (const char c: raw) {
		if (c != '_') s.push_back(c);
	}
	int base = 10;
	size_t start = 0;
	if (s.starts_with("0x") || s.starts_with("0X")) {
		base = 16;
		start = 2;
	} else if (s.starts_with("0b") || s.starts_with("0B")) {
		base = 2;
		start = 2;
	} else if (s.starts_with("0o") || s.starts_with("0O")) {
		base = 8;
		start = 2;
	}

	if (base == 16) {
		if (s.ends_with("UL") || s.ends_with("US") || s.ends_with("UB") || s.ends_with("UZ")) {
			s.pop_back(); s.pop_back();
		} else if (s.ends_with("U") || s.ends_with("L") || s.ends_with("S") || s.ends_with("Z")) {
			s.pop_back();
		} else if (raw.find('_') != std::string_view::npos) {
			size_t last_us = raw.rfind('_');
			std::string_view suf = raw.substr(last_us + 1);
			if (suf == "B" || suf == "D" || suf == "F") {
				s.pop_back();
			}
		}
	} else {
		if (s.ends_with("UL") || s.ends_with("US") || s.ends_with("UB") || s.ends_with("UZ")) {
			s.pop_back(); s.pop_back();
		} else if (s.ends_with("U") || s.ends_with("L") || s.ends_with("S") || s.ends_with("B") ||
		           s.ends_with("Z") || s.ends_with("D") || s.ends_with("F")) {
			s.pop_back();
		}
	}

	const char *begin = s.c_str() + start;
	char *end = nullptr;
	unsigned long long val = std::strtoull(begin, &end, base);
	return static_cast<int64_t>(val);
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
	std::string module_name;
	size_t line = 0;
	size_t col = 0;
};

export struct StructSymbol {
	std::string name;
	StringMap<Semantic> field_types;
	std::vector<std::string> field_order;
	StringMap<FnSymbol> methods;
	bool is_pub = false;
	std::string module_name;
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
