module;

#include <map>
#include <string_view>

export module map;

import token;

export inline const std::map<std::string_view, TokenType> KEYWORDS = {
	{"extern", TokenType::KW_EXTERN},
	{"struct", TokenType::KW_STRUCT},
	{"enum", TokenType::KW_ENUM},
	{"fn", TokenType::KW_FN},

	{"return", TokenType::KW_RETURN},
	{"const", TokenType::KW_CONST},
	{"null", TokenType::KW_NULL},
	{"true", TokenType::KW_TRUE},
	{"false", TokenType::KW_FALSE},
	{"val", TokenType::KW_VAL},
	{"var", TokenType::KW_VAR},
	{"as", TokenType::KW_AS},

	{"while", TokenType::KW_WHILE},
	{"break", TokenType::KW_BREAK},
	{"continue", TokenType::KW_CONTINUE},
	{"if", TokenType::KW_IF},
	{"else", TokenType::KW_ELSE},
	{"when", TokenType::KW_WHEN},
	{"mod", TokenType::KW_MOD},
	{"use", TokenType::KW_USE},
	{"pub", TokenType::KW_PUB},
	{"trait", TokenType::KW_TRAIT},
	{"override", TokenType::KW_OVERRIDE},
	{"impl", TokenType::KW_IMPL},
	{"for", TokenType::KW_FOR}
};
