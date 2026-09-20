module kobel;

import :lexer.Lexer;

namespace kobel::lexer {

	Token Lexer::make_token(const TokenType type) const {
		return {
			.type = type,
			.text = sub(cursor),
			.line = line,
			.col = col
		};
	}

	Token Lexer::next_token() {
		while (true) {
			skip_space();

			if (is_end()) return make_token(TokenType::END_OF_FILE);

			switch (const auto c = next()) {
				case ',':
					return make_token(TokenType::COMMA);
				case '.':
					return make_token(TokenType::DOT);
				case ':':
					return make_token(TokenType::COLON);
				case ';':
					return make_token(TokenType::SEMICOLON);
				case '(':
					return make_token(TokenType::LPAREN);
				case ')':
					return make_token(TokenType::RPAREN);
				case '{':
					return make_token(TokenType::LBRACE);
				case '}':
					return make_token(TokenType::RBRACE);
				case '[':
					return make_token(TokenType::LBRACK);
				case ']':
					return make_token(TokenType::RBRACK);
				case '%':
					return make_token(TokenType::PERCENT);
				case '+':
					return make_token(TokenType::PLUS);
				case '*':
					return make_token(TokenType::STAR);

				case '&':
					if (match('&')) return make_token(TokenType::AND);
					return make_token(TokenType::AMP);

				case '|':
					if (match('|')) return make_token(TokenType::OR);
					return make_token(TokenType::PIPE);

				case '<':
					if (match('=')) return make_token(TokenType::LESS_EQUAL);
					return make_token(TokenType::LESS);

				case '>':
					if (match('=')) return make_token(TokenType::GREATER_EQUAL);
					return make_token(TokenType::GREATER);

				case '-':
					if (match('>')) return make_token(TokenType::ARROW);
					return make_token(TokenType::MINUS);

				case '!':
					if (match('=')) return make_token(TokenType::EXCL_EQUAL);
					return make_token(TokenType::EXCL);

				case '=':
					if (match('=')) return make_token(TokenType::EQUALS);
					if (match('>')) return make_token(TokenType::FAT_ARROW);
					return make_token(TokenType::EQUAL);

				default:
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
