export module token;

import std;

export enum class TokenType {
	// Single syntax
	SEMI_COLON, // ;
	COLON, // :
	COMMA, // ,
	DOT, // .
	BANG, // !
	AT_SIGN, // @
	QUESTION, // ?
	EQUAL, // =
	PLUS, // +
	MINUS, // -
	STAR, // *
	SLASH, // /
	PERCENT, // %
	GREATER, // >
	LESS, // <
	OPEN_BRACKET, CLOSE_BRACKET, // [ and ]
	OPEN_PAREN, CLOSE_PAREN, // ( and )
	OPEN_BRACE, CLOSE_BRACE, // { and }

	// Double syntax
	// Expressions
	ARROW, // ->
	FAT_ARROW, // =>
	// Logics
	OR_OR, // ||
	AND_AND, // &&
	// Comparatives
	EQUAL_EQUAL, // ==
	NOT_EQUAL, // !=
	GREATER_OR_EQUAL, // >=
	LESS_OR_EQUAL, // <=

	// Datas
	CHAR, // 'a' 'A'
	NUMBER, // 1 2 3 4...
	STRING, // "Hello" "ABC"
	IDENTIFIER,

	// Keywords
	KW_EXTERN, // extern
	KW_STRUCT, // struct
	KW_TRAIT, // trait
	KW_FAULT, // fault
	KW_ENUM, // enum
	KW_FN, // fn

	KW_RETURN, // return
	KW_DEFER, // defer
	KW_CONST, // const
	KW_NULL, // null
	KW_VAL, // val
	KW_VAR, // var
	KW_AS, // as

	KW_CONTINUE, // continue
	KW_WHILE, // while
	KW_BREAK, // break
	KW_WHEN, // when
	KW_LOOP, // loop
	KW_ELSE, // else
	KW_FOR, // for
	KW_IN, // in
	KW_IF, // if

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
