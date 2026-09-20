module;

#include <vector>
#include <string_view>

export module kobel:lexer.Lexer;

import :lexer.Token;
import :lexer.TokenType;

export namespace kobel::lexer {

	class Lexer {
	private:
		std::string_view src;
		size_t cursor = 0;
		uint32_t line = 1;
		uint32_t col = 1;

	public:
		bool is_end() const;

		bool match(char expected);

		char peek() const;

		char next();

		std::string_view sub(size_t start) const;

		void new_line();

		void skip_space();

		Token make_token(TokenType type) const;

		Token next_token();

		Token scan_number();

		Token scan_char();

		Token scan_string();

		Token scan_identifier();

		std::vector<Token> tokenize();
	};

}
