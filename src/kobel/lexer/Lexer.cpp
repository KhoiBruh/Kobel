module;

#include <string_view>

module kobel;

import :lexer.Lexer;

namespace kobel::lexer {

	Lexer::Lexer(const std::string_view src) : src(src) {
	}

	bool Lexer::is_end() const {
		return cursor >= src.size();
	}

	bool Lexer::match(const char expected) {
		if (is_end() || src[cursor] != expected) return false;
		cursor++;
		col++;
		return true;
	}

	char Lexer::peek(const size_t offset) const {
		if (cursor + offset >= src.size()) return '\0';
		return src[cursor + offset];
	}

	char Lexer::next() {
		if (is_end()) return '\0';
		col++;
		return src[cursor++];
	}

	std::string_view Lexer::sub(const size_t start) const {
		return src.substr(start, cursor - start);
	}

	void Lexer::new_line() {
		if (peek() == '\r') {
			next();
			if (peek() == '\n') next();
		} else if (peek() == '\n') next();

		line++;
		col = 1;
	}

	void Lexer::skip_space() {
		while (!is_end()) {
			switch (peek()) {
				case ' ':
				case '\t':
					next();
					break;

				case '\r':
				case '\n':
					new_line();
					break;

				default:
					return;
			}
		}
	}

}
