module;

#include <cctype>
#include <vector>
#include <string_view>

export module lexer;

import token;
import token.map;

export struct Lexer {
	std::string_view src;
	size_t cursor = 0;
	uint32_t line = 1;
	uint32_t col = 1;

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
		const uint32_t start_col
	) const {
		return {type, sub(start_cursor), line, start_col};
	}

	void handle_newline() {
		if (peek() == '\r') {
			next();
			if (!is_end() && peek() == '\n') {
				cursor++;
			}
		} else if (peek() == '\n') {
			cursor++;
		}
		line++;
		col = 1;
	}

	void skip_whitespace() {
		while (!is_end()) {
			const char c = peek();
			if (c == ' ' || c == '\t') {
				next();
			} else if (c == '\r' || c == '\n') {
				handle_newline();
			} else break;
		}
	}

	void scan_decimal_digits_and_fraction() {
		while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
		if (
			peek() == '.' &&
			cursor + 1 < src.size() &&
			std::isdigit(static_cast<unsigned char>(src[cursor + 1]))
		) {
			next(); // consume '.'
			while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
		}
	}

	Token scan_identifier(size_t start_cursor, uint32_t start_col) {
		while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') next();
		const auto text = sub(start_cursor);
		return make_token(lookup_keyword(text), start_cursor, start_col);
	}

	Token scan_number(size_t start_cursor, uint32_t start_col) {
		const char first = src[start_cursor];
		if (first == '0' && !is_end()) {
			const char p = peek();
			if (p == 'x' || p == 'X') {
				next(); // consume 'x' or 'X'
				while (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
			} else if (p == 'b' || p == 'B') {
				next(); // consume 'b' or 'B'
				while (peek() == '0' || peek() == '1' || peek() == '_') next();
			} else if (p == 'o' || p == 'O') {
				next(); // consume 'o' or 'O'
				while ((peek() >= '0' && peek() <= '7') || peek() == '_') next();
			} else {
				scan_decimal_digits_and_fraction();
			}
		} else {
			scan_decimal_digits_and_fraction();
		}

		// Optional separator before suffix: e.g. 100_L or 0xFF_UL
		if (peek() == '_' && cursor + 1 < src.size()) {
			const char next_c = src[cursor + 1];
			if (next_c == 'S' || next_c == 'L' || next_c == 'B' || next_c == 'Z' ||
			    next_c == 'U' || next_c == 'D' || next_c == 'F') {
				next(); // consume '_'
			}
		}

		// Optional type suffixes: S, L, B, Z, U, US, UL, UB, UZ, D, F
		if (!is_end()) {
			const char c1 = peek();
			if (c1 == 'U' && cursor + 1 < src.size()) {
				const char c2 = src[cursor + 1];
				if (c2 == 'S' || c2 == 'L' || c2 == 'B' || c2 == 'Z') {
					next();
					next();
				} else {
					next();
				}
			} else if (c1 == 'S' || c1 == 'L' || c1 == 'B' || c1 == 'Z' ||
			           c1 == 'U' || c1 == 'D' || c1 == 'F') {
				next();
			}
		}

		return make_token(TokenType::NUMBER, start_cursor, start_col);
	}

	Token scan_string(size_t start_cursor, uint32_t start_col) {
		bool closed = false;
		while (!is_end()) {
			const char c = peek();
			if (c == '"') {
				next();
				closed = true;
				break;
			}
			if (c == '\\') {
				next(); // consume '\'
				if (!is_end()) {
					if (peek() == '\r' || peek() == '\n') {
						handle_newline();
					} else {
						next(); // consume escaped char
					}
				}
				continue;
			}
			if (c == '\r' || c == '\n') {
				handle_newline();
				continue;
			}
			next();
		}
		if (!closed) {
			return make_token(TokenType::UNKNOWN, start_cursor, start_col);
		}
		return make_token(TokenType::STRING, start_cursor, start_col);
	}

	Token scan_char(size_t start_cursor, uint32_t start_col) {
		if (!is_end() && peek() == '\\') next();
		if (!is_end()) next();
		if (!is_end() && peek() == '\'') next();
		return make_token(TokenType::CHAR, start_cursor, start_col);
	}

	Token next_token() {
		while (true) {
			skip_whitespace();
			if (is_end()) return {TokenType::END_OF_FILE, "", line, col};

			const size_t start_cursor = cursor;
			const uint32_t start_col = col;

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
					if (match('>')) return make_token(TokenType::FAT_ARROW, start_cursor, start_col);
					return make_token(TokenType::EQUAL, start_cursor, start_col);

				case '-':
					if (match('>')) return make_token(TokenType::ARROW, start_cursor, start_col);
					return make_token(TokenType::MINUS, start_cursor, start_col);

				case '/':
					if (match('/')) {
						while (!is_end() && peek() != '\n' && peek() != '\r') next();
						continue;
					}
					if (match('*')) {
						while (!is_end()) {
							if (peek() == '\r' || peek() == '\n') {
								handle_newline();
								continue;
							}
							if (peek() == '*' && cursor + 1 < src.size() && src[cursor + 1] == '/') {
								next(); // *
								next(); // /
								break;
							}
							next();
						}
						continue;
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
					return make_token(TokenType::AMPERSAND, start_cursor, start_col);

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
