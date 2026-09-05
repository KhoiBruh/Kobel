export module map;

#include <map>
#include <string_view>

import token;

export inline const std::map<std::string_view, TokenType> KEYWORDS = {
	{"extern", TokenType::KW_EXTERN},
	{"struct", TokenType::KW_STRUCT},
	{"trait", TokenType::KW_TRAIT},
	{"fault", TokenType::KW_FAULT},
	{"enum", TokenType::KW_ENUM},
	{"fn", TokenType::KW_FN},

	{"return", TokenType::KW_RETURN},
	{"defer", TokenType::KW_DEFER},
	{"const", TokenType::KW_CONST},
	{"null", TokenType::KW_NULL},
	{"val", TokenType::KW_VAL},
	{"var", TokenType::KW_VAR},
	{"as", TokenType::KW_AS},

	{"continue", TokenType::KW_CONTINUE},
	{"while", TokenType::KW_WHILE},
	{"break", TokenType::KW_BREAK},
	{"when", TokenType::KW_WHEN},
	{"loop", TokenType::KW_LOOP},
	{"else", TokenType::KW_ELSE},
	{"for", TokenType::KW_FOR},
	{"if", TokenType::KW_IF},
	{"in", TokenType::KW_IN}
};
