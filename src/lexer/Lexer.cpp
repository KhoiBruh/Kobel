module;

#include <cctype>
#include <vector>
#include <string_view>

export module lexer;

import token;
import map;

export struct Lexer {
	std::string_view src;
	size_t cursor = 0;
	size_t line = 1;
	size_t col = 1;

	bool is_end() const {
		return cursor >= src.size();
	}

	char peek() const {
		return is_end() ? '\0' : src[cursor];
	}

	std::string_view sub(const size_t start) const {
		return src.substr(start, cursor - start);
	}

	char next() {
		if (is_end()) return '\0';
		col++;
		return src[cursor++];
	}

	bool match(const char expected) {
		if (is_end() || src[cursor] != expected) return false;
		cursor++;
		col++;
		return true;
	}

	Token make_token(
		const TokenType type,
		const size_t start_cursor,
		const size_t start_col
	) const {
		return {type, sub(start_cursor), line, start_col};
	}

	void skip_whitespace() {
		while (!is_end()) {
			if (
				const char c = peek();
				c == ' ' ||
				c == '\t' ||
				c == '\r'
			) next();
			else if (c == '\n') {
				line++;
				col = 1;
				cursor++;
			} else break;
		}
	}

	Token scan_identifier(size_t start_cursor, size_t start_col) {
		while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') next();
		const auto text = sub(start_cursor);
		const auto it = KEYWORDS.find(text);
		const auto type = it != KEYWORDS.end() ? it->second : TokenType::IDENTIFIER;
		return {type, text, line, start_col};
	}

	Token scan_number(size_t start_cursor, size_t start_col) {
		while (std::isdigit(static_cast<unsigned char>(peek()))) next();
		if (
			peek() == '.' &&
			cursor + 1 < src.size() &&
			std::isdigit(static_cast<unsigned char>(src[cursor + 1]))
		) {
			next(); // nuốt '.'
			while (std::isdigit(static_cast<unsigned char>(peek()))) next();
		}

		return {TokenType::NUMBER, sub(start_cursor), line, start_col};
	}

	Token scan_string(size_t start_cursor, size_t start_col) {
		while (!is_end() && peek() != '"') {
			if (peek() == '\n') line++;
			next();
		}
		if (!is_end()) next();
		return {TokenType::STRING, sub(start_cursor), line, start_col};
	}

	Token scan_char(size_t start_cursor, size_t start_col) {
		if (!is_end() && peek() == '\\') next();
		if (!is_end()) next();
		if (!is_end() && peek() == '\'') next();
		return {TokenType::CHAR, sub(start_cursor), line, start_col};
	}

	Token next_token() {
		skip_whitespace();
		if (is_end()) return {TokenType::END_OF_FILE, "", line, col};

		const size_t start_cursor = cursor;
		const size_t start_col = col;

		switch (const char c = next()) {
			case ',': return make_token(TokenType::COMMA, start_cursor, start_col);
			case '.': return make_token(TokenType::DOT, start_cursor, start_col);
			case ':': return make_token(TokenType::COLON, start_cursor, start_col);
			case ';': return make_token(TokenType::SEMI_COLON, start_cursor, start_col);
			case '(': return make_token(TokenType::OPEN_PAREN, start_cursor, start_col);
			case ')': return make_token(TokenType::CLOSE_PAREN, start_cursor, start_col);
			case '{': return make_token(TokenType::OPEN_BRACE, start_cursor, start_col);
			case '}': return make_token(TokenType::CLOSE_BRACE, start_cursor, start_col);
			case '[': return make_token(TokenType::OPEN_BRACKET, start_cursor, start_col);
			case ']': return make_token(TokenType::CLOSE_BRACKET, start_cursor, start_col);
			case '%': return make_token(TokenType::PERCENT, start_cursor, start_col);
			case '+': return make_token(TokenType::PLUS, start_cursor, start_col);
			case '*': return make_token(TokenType::STAR, start_cursor, start_col);
			case '"': return scan_string(start_cursor, start_col);
			case '\'': return scan_char(start_cursor, start_col);

			case '=':
				if (match('=')) return make_token(TokenType::EQUAL_EQUAL, start_cursor, start_col);
				return make_token(TokenType::EQUAL, start_cursor, start_col);

			case '-':
				return make_token(TokenType::MINUS, start_cursor, start_col);

			case '/':
				if (match('/')) {
					while (!is_end() && peek() != '\n') next();
					return next_token();
				}
				if (match('*')) {
					while (!is_end()) {
						if (peek() == '\n') {
							line++;
							col = 1;
							cursor++;
							continue;
						}
						if (peek() == '*' && cursor + 1 < src.size() && src[cursor + 1] == '/') {
							next(); // *
							next(); // /
							break;
						}
						next();
					}
					return next_token();
				}
				return make_token(TokenType::SLASH, start_cursor, start_col);

			case '<':
				if (match('=')) return make_token(TokenType::LESS_EQUAL, start_cursor, start_col);
				return make_token(TokenType::LESS, start_cursor, start_col);

			case '>':
				if (match('=')) return make_token(TokenType::GREATER_EQUAL, start_cursor, start_col);
				return make_token(TokenType::GREATER, start_cursor, start_col);

			case '&':
				if (match('&')) return make_token(TokenType::AND_AND, start_cursor, start_col);
				return make_token(TokenType::UNKNOWN, start_cursor, start_col);

			case '!':
				if (match('=')) return make_token(TokenType::BANG_EQUAL, start_cursor, start_col);
				return make_token(TokenType::BANG, start_cursor, start_col);

			case '|':
				if (match('|')) return make_token(TokenType::OR_OR, start_cursor, start_col);
				return make_token(TokenType::UNKNOWN, start_cursor, start_col);

			default:
				if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
					return scan_identifier(start_cursor, start_col);
				if (std::isdigit(static_cast<unsigned char>(c)))
					return scan_number(start_cursor, start_col);
				return make_token(TokenType::UNKNOWN, start_cursor, start_col);
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
