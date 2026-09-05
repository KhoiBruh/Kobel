module;
#include <string_view>
export module token;

export enum class TokenType {
	// Single syntax
	SEMI_COLON,
	COLON,
	COMMA,
	DOT,
	BANG,
	AT_SIGN,
	QUESTION,
	EQUAL,
	PLUS,
	MINUS,
	STAR,
	SLASH,
	PERCENT,
	GREATER,
	LESS,
	QUOTE,
	DOUBLE_QUOTE,
	OPEN_BRACKET,
	CLOSE_BRACKET,
	OPEN_PAREN,
	CLOSE_PAREN,
	OPEN_BRACE,
	CLOSE_BRACE,

	// Double syntax
	GREATER_EQUAL,
	QUESTION_COLON,
	LESS_EQUAL,
	SLASH_SLASH,
	SLASH_EQUAL,
	MINUS_EQUAL,
	MINUS_MINUS,
	EQUAL_EQUAL,
	PLUS_EQUAL,
	STAR_EQUAL,
	PLUS_PLUS,
	BANG_EQUAL,
	FAT_ARROW,
	AND_AND,
	OR_OR,
	ARROW,

	// Literals
	CHAR,
	NUMBER,
	STRING,
	IDENTIFIER,

	// Keywords
	KW_EXTERN,
	KW_STRUCT,
	KW_TRAIT,
	KW_FAULT,
	KW_ENUM,
	KW_FN,

	KW_RETURN,
	KW_DEFER,
	KW_CONST,
	KW_NULL,
	KW_VAL,
	KW_VAR,
	KW_AS,

	KW_CONTINUE,
	KW_WHILE,
	KW_BREAK,
	KW_WHEN,
	KW_LOOP,
	KW_ELSE,
	KW_FOR,
	KW_IN,
	KW_IF,

	// Specials
	END_OF_FILE,
	UNKNOWN
};

export struct Token {
	TokenType type;
	std::string_view text;
	size_t line;
	size_t col;
};
