module;

#include <vector>
#include <string_view>

export module kobel:lexer.Lexer;

import :lexer.Token;
import :lexer.TokenType;

export namespace kobel::lexer {

	class Lexer {
		std::string_view src;
		size_t cursor = 0;
		uint32_t line = 1;
		uint32_t col = 1;

		size_t start_cursor = 0;
		uint32_t start_line = 1;
		uint32_t start_col = 1;

		void scan_decimals();

	public:
		Lexer() = default;

		explicit Lexer(std::string_view src);

		[[nodiscard]] bool is_end() const;

		bool match(char expected);

		[[nodiscard]] char peek(size_t offset = 0) const;

		char next();

		[[nodiscard]] std::string_view sub(size_t start) const;

		void new_line();

		void skip_space();

		[[nodiscard]] Token make_token(TokenType type) const;

		Token next_token();

		Token scan_number();

		Token scan_char();

		Token scan_string();

		Token scan_identifier();

		std::vector<Token> tokenize();
	};

}
