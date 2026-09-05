export module lexer;

import std;
import token;

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

	Token scan_identifier(size_t start_col) {
		const size_t start = cursor - 1;

		while (std::isalnum(peek() || peek() == '_')) next();

		const std::string_view text = src.substr(start, cursor - start);

		TokenType type;
		switch (text) {
			case "const":
				type = TokenType::KW_CONST;
				break;
			case "val":
				type = TokenType::KW_VAL;
				break;
			case "var":
				type = TokenType::KW_VAR;
				break;
			case "return":
				type = TokenType::KW_RETURN;
				break;
			default:
				type = TokenType::IDENTIFIER;
				break;
		}

		return {type, text, line, start_col};
	}

	Token next_token() {
		skip_whitespace();

		if (is_end()) return Token(TokenType::END_OF_FILE, "", line, col);

		const size_t start = cursor;
		char c = next();

		switch (c) {
			default:
				return {
					TokenType::UNKNOWN,
					src.substr(cursor - 1, 1),
					line, start
				};
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
