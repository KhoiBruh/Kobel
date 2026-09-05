module;

#include <string_view>

export module token;

export enum class TokenType {
	// Single syntax
	SEMI_COLON,    // ;
	COLON,         // :
	COMMA,         // ,
	DOT,           // .
	BANG,          // !
	EQUAL,         // =
	PLUS,          // +
	MINUS,         // -
	STAR,          // *
	SLASH,         // /
	PERCENT,       // %
	GREATER,       // >
	LESS,          // <
	OPEN_BRACKET,  // [
	CLOSE_BRACKET, // ]
	OPEN_PAREN,    // (
	CLOSE_PAREN,   // )
	OPEN_BRACE,    // {
	CLOSE_BRACE,   // }

	// Double syntax
	EQUAL_EQUAL,   // ==
	BANG_EQUAL,    // !=
	LESS_EQUAL,    // <=
	GREATER_EQUAL, // >=
	AND_AND,       // &&
	OR_OR,         // ||

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
