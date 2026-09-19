module;

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

export module token;

export enum class TokenType : uint8_t {
	// Single syntax
	SEMI_COLON, // ;
	COLON, // :
	COMMA, // ,
	DOT, // .
	BANG, // !
	EQUAL, // =
	PLUS, // +
	MINUS, // -
	STAR, // *
	SLASH, // /
	PERCENT, // %
	GREATER, // >
	LESS, // <
	OPEN_BRACKET, // [
	CLOSE_BRACKET, // ]
	OPEN_PAREN, // (
	CLOSE_PAREN, // )
	OPEN_BRACE, // {
	CLOSE_BRACE, // }
	AMPERSAND, // &

	// Double syntax
	EQUAL_EQUAL, // ==
	BANG_EQUAL, // !=
	LESS_EQUAL, // <=
	GREATER_EQUAL, // >=
	AND_AND, // &&
	OR_OR, // ||
	FAT_ARROW, // =>
	ARROW, // ->

	// Literals
	CHAR,
	NUMBER,
	STRING,
	IDENTIFIER,

	// Keywords
	KW_EXTERN,
	KW_STRUCT,
	KW_ENUM,
	KW_FN,
	KW_RETURN,
	KW_CONST,
	KW_NULL,
	KW_TRUE,
	KW_FALSE,
	KW_VAL,
	KW_VAR,
	KW_AS,
	KW_WHILE,
	KW_BREAK,
	KW_CONTINUE,
	KW_IF,
	KW_ELSE,
	KW_WHEN,
	KW_MOD,
	KW_USE,
	KW_PUB,
	KW_TRAIT,
	KW_IMPL,
	KW_FOR,

	// Specials
	END_OF_FILE,
	UNKNOWN
};

export struct Token {
	TokenType type;
	std::string_view text;
	uint32_t line;
	uint32_t col;
};

export inline int64_t parse_kobel_int(std::string_view raw) {
	if (raw.find('.') != std::string_view::npos) {
		return -1;
	}
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
			if (suf == "B") {
				s.pop_back();
			}
		}
	} else {
		if (s.ends_with("UL") || s.ends_with("US") || s.ends_with("UB") || s.ends_with("UZ")) {
			s.pop_back(); s.pop_back();
		} else if (s.ends_with("U") || s.ends_with("L") || s.ends_with("S") || s.ends_with("B") ||
		           s.ends_with("Z")) {
			s.pop_back();
		}
	}

	const char *begin = s.data() + start;
	const char *end = s.data() + s.size();
	unsigned long long val = 0;
	if (begin < end) {
		auto [ptr, ec] = std::from_chars(begin, end, val, base);
		if (ec != std::errc{} || ptr != end) {
			return -1;
		}
	} else {
		return -1;
	}
	return static_cast<int64_t>(val);
}
