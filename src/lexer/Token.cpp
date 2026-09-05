export module token;

import std;

export enum class TokenType {
	// Single syntax
	SEMI_COLON, COMMA, DOUBLE_COLON, AT_SIGN,
	OPEN_BRACKET, CLOSE_BRACKET, // { and }
	OPEN_PAREN, CLOSE_PAREN, // ( and )
	OPEN_BRACE, CLOSE_BRACE, // [ and ]

	// Double syntax
	INFERENCE, // ->
	SINGLE_EXPR, // =>

	// Logics
	NOT, // !
	OR_OR, // ||
	AND_AND, // &&

	// Datas
	CHAR, // 'a' 'A'
	NUMBER, // 1 2 3 4...
	STRING, // "Hello" "ABC"
	NULLABLE, // Type?
	IDENTIFIER,

	// Keywords
	KW_STRUCT, // struct
	KW_TRAIT, // trait
	KW_FAULT, // fault
	KW_FN, // fn

	// field declarations
	KW_CONST, // const
	KW_VAL, // val
	KW_VAR, // var

	// control flows
	KW_IF, // if
	KW_ELSE, // else
	KW_FOR, // for
	KW_LOOP, // loop
	KW_WHILE, // while
	KW_IN, // in

	KW_RETURN, // return

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
