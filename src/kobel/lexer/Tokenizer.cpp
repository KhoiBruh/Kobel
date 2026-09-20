module;

#include <cctype>
#include <string_view>
#include <unordered_map>
#include <vector>

module kobel;

import :lexer.Lexer;
import :lexer.Token;
import :lexer.TokenType;

namespace kobel::lexer {

	static TokenType lookup_keyword(const std::string_view text) noexcept {
		static const std::unordered_map<std::string_view, TokenType> KEYWORDS = {
			{ "extern", TokenType::KW_EXTERN },
			{ "struct", TokenType::KW_STRUCT },
			{ "enum", TokenType::KW_ENUM },
			{ "fn", TokenType::KW_FN },
			{ "return", TokenType::KW_RETURN },
			{ "const", TokenType::KW_CONST },
			{ "null", TokenType::KW_NULL },
			{ "true", TokenType::KW_TRUE },
			{ "false", TokenType::KW_FALSE },
			{ "val", TokenType::KW_VAL },
			{ "var", TokenType::KW_VAR },
			{ "as", TokenType::KW_AS },
			{ "while", TokenType::KW_WHILE },
			{ "break", TokenType::KW_BREAK },
			{ "continue", TokenType::KW_CONTINUE },
			{ "if", TokenType::KW_IF },
			{ "else", TokenType::KW_ELSE },
			{ "when", TokenType::KW_WHEN },
			{ "mod", TokenType::KW_MOD },
			{ "use", TokenType::KW_USE },
			{ "pub", TokenType::KW_PUB },
			{ "trait", TokenType::KW_TRAIT },
			{ "impl", TokenType::KW_IMPL },
			{ "for", TokenType::KW_FOR }
		};

		const auto it = KEYWORDS.find(text);
		return it != KEYWORDS.end() ? it->second : TokenType::IDENTIFIER;
	}

	Token Lexer::make_token(const TokenType type) const {
		return {
			.type = type,
			.text = sub(start_cursor),
			.line = start_line,
			.col = start_col
		};
	}

	void Lexer::scan_decimals() {
		while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
		if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
			next(); // consume '.'
			while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
		}
	}

	Token Lexer::scan_number() {
		if (
			const char first = src[start_cursor];
			first == '0' && !is_end()
		) {
			if (const char p = peek(); p == 'x' || p == 'X') {
				next(); // consume 'x' or 'X'
				while (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '_') next();
			} else if (p == 'b' || p == 'B') {
				next(); // consume 'b' or 'B'
				while (peek() == '0' || peek() == '1' || peek() == '_') next();
			} else if (p == 'o' || p == 'O') {
				next(); // consume 'o' or 'O'
				while ((peek() >= '0' && peek() <= '7') || peek() == '_') next();
			} else scan_decimals();
		} else scan_decimals();

		// Optional separator before suffix: e.g. 100_L or 0xFF_UL
		if (peek() == '_') {
			if (
				const char next_c = peek(1);
				next_c == 'S' || next_c == 'L' || next_c == 'B' ||
				next_c == 'Z' || next_c == 'U' ||
				next_c == 'D' || next_c == 'F'
			)
				next(); // consume '_'
		}

		// Optional type suffixes: S, L, B, Z, U, US, UL, UB, UZ, D, F
		if (!is_end()) {
			if (const char c1 = peek(); c1 == 'U') {
				if (
					const char c2 = peek(1);
					c2 == 'S' || c2 == 'L' ||
					c2 == 'B' || c2 == 'Z'
				) {
					next();
					next();
				} else next();
			} else if (
				c1 == 'S' || c1 == 'L' ||
				c1 == 'B' || c1 == 'Z' ||
				c1 == 'D' || c1 == 'F'
			)
				next();
		}

		return make_token(TokenType::NUMBER);
	}

	Token Lexer::scan_char() {
		if (!is_end() && peek() == '\\') next();
		if (!is_end()) next();
		if (!is_end() && peek() == '\'') next();
		return make_token(TokenType::CHAR);
	}

	Token Lexer::scan_string() {
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
					if (peek() == '\r' || peek() == '\n') new_line();
					else next(); // consume escaped char
				}
				continue;
			}
			if (c == '\r' || c == '\n') {
				new_line();
				continue;
			}
			next();
		}

		if (!closed) return make_token(TokenType::UNKNOWN);
		return make_token(TokenType::STRING);
	}

	Token Lexer::scan_identifier() {
		while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') next();
		const auto text = sub(start_cursor);
		return make_token(lookup_keyword(text));
	}

	Token Lexer::next_token() {
		while (true) {
			skip_space();

			if (is_end()) {
				start_cursor = cursor;
				start_line = line;
				start_col = col;
				return make_token(TokenType::END_OF_FILE);
			}

			start_cursor = cursor;
			start_line = line;
			start_col = col;

			switch (const auto c = next()) {
				case ',': return make_token(TokenType::COMMA);
				case '.': return make_token(TokenType::DOT);
				case ':': return make_token(TokenType::COLON);
				case ';': return make_token(TokenType::SEMICOLON);
				case '(': return make_token(TokenType::LPAREN);
				case ')': return make_token(TokenType::RPAREN);
				case '{': return make_token(TokenType::LBRACE);
				case '}': return make_token(TokenType::RBRACE);
				case '[': return make_token(TokenType::LBRACK);
				case ']': return make_token(TokenType::RBRACK);
				case '%': return make_token(TokenType::PERCENT);
				case '+': return make_token(TokenType::PLUS);
				case '*': return make_token(TokenType::STAR);
				case '"': return scan_string();
				case '\'': return scan_char();

				case '&': if (match('&')) return make_token(TokenType::AND);
					return make_token(TokenType::AMP);

				case '|': if (match('|')) return make_token(TokenType::OR);
					return make_token(TokenType::PIPE);

				case '<': if (match('=')) return make_token(TokenType::LESS_EQUAL);
					return make_token(TokenType::LESS);

				case '>': if (match('=')) return make_token(TokenType::GREATER_EQUAL);
					return make_token(TokenType::GREATER);

				case '-': if (match('>')) return make_token(TokenType::ARROW);
					return make_token(TokenType::MINUS);

				case '!': if (match('=')) return make_token(TokenType::EXCL_EQUAL);
					return make_token(TokenType::EXCL);

				case '=': if (match('=')) return make_token(TokenType::EQUALS);
					if (match('>')) return make_token(TokenType::FAT_ARROW);
					return make_token(TokenType::EQUAL);

				case '/': if (match('/')) {
						while (!is_end() && peek() != '\n' && peek() != '\r') next();
						continue;
					}
					if (match('*')) {
						while (!is_end()) {
							if (peek() == '\r' || peek() == '\n') {
								new_line();
								continue;
							}
							if (peek() == '*' && peek(1) == '/') {
								next(); // *
								next(); // /
								break;
							}
							next();
						}
						continue;
					}
					return make_token(TokenType::SLASH);

				default: const auto uc = static_cast<unsigned char>(c);
					if (std::isalpha(uc) || c == '_') return scan_identifier();
					if (std::isdigit(uc)) return scan_number();
					return make_token(TokenType::UNKNOWN);
			}
		}
	}

	std::vector<Token> Lexer::tokenize() {
		std::vector<Token> tokens;
		while (true) {
			const auto token = next_token();
			tokens.push_back(token);
			if (token.type == TokenType::END_OF_FILE) break;
		}

		return tokens;
	}

}
