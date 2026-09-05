export module lexer;

#include <cctype>
#include <string_view>
#include <vector>

import token;
import map;

export struct Lexer {
	std::string_view src;
	size_t cursor = 0;
	size_t line = 1;
	size_t col = 1;

	bool is_end() const {
		return cursor == src.size();
	}

	char peek() const {
		if (is_end()) return '\0';
		return src[cursor];
	}

	char next() {
		if (is_end()) return '\0';
		const char c = src[cursor++];
		col++;
		return c;
	}

	void skip_whitespace() {
		while (!is_end()) {
			if (
				const char c = peek();
				c == ' ' ||
				c == '\t' ||
				c == '\r'
			)
				next();
			else if (c == '\n') {
				line++;
				col = 1;
				cursor++;
			} else break;
		}
	}

	// Token things

	Token scan_identifier(const size_t start_cursor, const size_t start_col) {
		const auto peek = peek();
		while (std::isalnum(static_cast<unsigned char>(peek)) || peek == '_') next();

		const auto text = src.substr(start_cursor, cursor - start_cursor);
		const auto it = KEYWORD.find(text);

		const auto type = it != KEYWORD.end() ? it->second : TokenType::IDENTIFIER;
		return {type, text, line, start_col};
	}

	Token next_token() {
		skip_whitespace();

		if (is_end()) return {TokenType::END_OF_FILE, "", line, col};

		const size_t start = cursor;
		const std::string_view sub = src.substr(cursor - 1, 1);
		switch (const char c = next()) {
			case '{':
				return {TokenType::OPEN_BRACE, "{", line, col};

			case '}':
				return {TokenType::CLOSE_BRACE, "}", line, col};

			case '(':
				return {TokenType::OPEN_PAREN, sub, line, start};

			case ')':
				return {TokenType::CLOSE_PAREN, sub, line, start};

			default:
				if (std::isalpha(c) || c == '_') return scan_identifier(start);
				return {TokenType::UNKNOWN, sub, line, start};
		}
	}

	std::vector<Token> tokenize() {
		std::vector<Token> tokens;

		while (true) {
			Token token = next_token();
			tokens.push_back(token);
			if (token.type == TokenType::END_OF_FILE) break;
		}

		return tokens;
	}
};
