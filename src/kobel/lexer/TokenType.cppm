export module kobel:lexer.TokenType;

export namespace kobel::lexer {

	enum class TokenType {
		// Single syntax
		SEMICOLON, // ;
		COLON, // :
		COMMA, // ,
		DOT, // .
		EXCL, // !
		EQUAL, // =
		PLUS, // +
		MINUS, // -
		STAR, // *
		SLASH, // /
		PERCENT, // %
		GREATER, // >
		LESS, // <
		LBRACK, // [
		RBRACK, // ]
		LPAREN, // (
		RPAREN, // )
		LBRACE, // {
		RBRACE, // }
		PIPE, // |
		AMP, // &

		// Double syntax
		EQUALS, // ==
		EXCL_EQUAL, // !=
		LESS_EQUAL, // <=
		GREATER_EQUAL, // >=
		AND, // &&
		OR, // ||
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

}
