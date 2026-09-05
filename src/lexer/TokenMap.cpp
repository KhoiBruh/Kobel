export module map;

#include <map>
#include <string_view>

import token;

export static const std::map<std::string_view, TokenType> KEYWORD = {
	{"const", TokenType::KW_CONST},
	{"val", TokenType::KW_VAL},
	{"var", TokenType::KW_VAR},
	{"return", TokenType::KW_RETURN},
	{"if", TokenType::KW_IF},
	{"else", TokenType::KW_ELSE},
	{"for", TokenType::KW_FOR},
	{"while", TokenType::KW_WHILE},
	{"break", TokenType::KW_BREAK},
	{"loop", TokenType::KW_LOOP},
	{"in", TokenType::KW_IN}
};

export static const std::map<std::string_view, TokenType> SINGLE_SYNTAX = {
	{"=", TokenType::EQUAL},
};
