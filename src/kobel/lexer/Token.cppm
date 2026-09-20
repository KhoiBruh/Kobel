module;

#include "string_view"

export module kobel:lexer.Token;

import :lexer.TokenType;

export namespace kobel::lexer {

	struct Token {
		TokenType type;
		std::string_view text;
		uint32_t line;
		uint32_t col;
	};

}
