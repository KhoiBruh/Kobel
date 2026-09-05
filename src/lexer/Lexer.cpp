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

	bool match(const char expected) {
		if (is_end() || src[cursor] != expected) return false;
		cursor++;
		col++;
		return true;
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

	Token make_token(const TokenType type) const {
		const auto text = src.substr(cursor - 1, 1);
		return {type, text, line, col};
	}

	Token scan_identifier(const size_t start_cursor, const size_t start_col) {
		const auto peek = peek();
		while (std::isalnum(static_cast<unsigned char>(peek)) || peek == '_') next();

		const auto text = src.substr(start_cursor, cursor - start_cursor);
		const auto it = KEYWORDS.find(text);

		const auto type = it != KEYWORDS.end() ? it->second : TokenType::IDENTIFIER;
		return {type, text, line, start_col};
	}

	Token next_token() {
		skip_whitespace();

		if (is_end()) return {TokenType::END_OF_FILE, "", line, col};

		const auto start = cursor;
		switch (const char c = next()) {
			case ',':
				return make_token(TokenType::COMMA);

			case '.':
				return make_token(TokenType::DOT);

			case ':':
				return make_token(TokenType::COLON);

			case ';':
				return make_token(TokenType::SEMI_COLON);

			case '"':
				return make_token(TokenType::DOUBLE_QUOTE);

			case '?':
				if (match(':')) return make_token(TokenType::QUESTION_COLON);
				return make_token(TokenType::QUESTION);

			case '{':
				return make_token(TokenType::OPEN_BRACE);

			case '}':
				return make_token(TokenType::CLOSE_BRACE);

			case '(':
				return make_token(TokenType::OPEN_PAREN);

			case ')':
				return make_token(TokenType::CLOSE_PAREN);

			case '[':
				return make_token(TokenType::OPEN_BRACKET);

			case ']':
				return make_token(TokenType::CLOSE_BRACKET);

			case '%':
				return make_token(TokenType::PERCENT);

			case '=':
				if (match('=')) return make_token(TokenType::EQUAL_EQUAL);
				if (match('>')) return make_token(TokenType::FAT_ARROW);
				return make_token(TokenType::EQUAL);

			case '-':
				if (match('=')) return make_token(TokenType::MINUS_EQUAL);
				if (match('-')) return make_token(TokenType::MINUS_MINUS);
				if (match('>')) return make_token(TokenType::ARROW);
				return make_token(TokenType::MINUS);

			case '+':
				if (match('=')) return make_token(TokenType::PLUS_EQUAL);
				if (match('+')) return make_token(TokenType::PLUS_PLUS);
				return make_token(TokenType::PLUS);

			case '*':
				if (match('=')) return make_token(TokenType::STAR_EQUAL);
				return make_token(TokenType::STAR);

			case '/':
				if (match('=')) return make_token(TokenType::SLASH_SLASH);
				if (match('=')) return make_token(TokenType::SLASH_EQUAL);
				return make_token(TokenType::SLASH);

			case '<':
				if (match('=')) return make_token(TokenType::LESS_EQUAL);
				return make_token(TokenType::LESS);

			case '>':
				if (match('=')) return make_token(TokenType::GREATER_EQUAL);
				return make_token(TokenType::GREATER);

			case '&':
				if (match('&')) return make_token(TokenType::AND_AND);
				return make_token(TokenType::UNKNOWN);

			case '!':
				if (match('=')) return make_token(TokenType::BANG_EQUAL);
				return make_token(TokenType::BANG);

			case '|':
				if (match('|')) return make_token(TokenType::OR_OR);
				return make_token(TokenType::UNKNOWN);

			default:
				if (std::isalpha(c) || c == '_') return scan_identifier(start, col++);
				return {TokenType::UNKNOWN, src.substr(cursor - 1, 1), line, start};
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
