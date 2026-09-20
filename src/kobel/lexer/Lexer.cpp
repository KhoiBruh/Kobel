module;

#include <string_view>

module kobel;

import :lexer.Lexer;

namespace kobel::lexer {

	bool Lexer::is_end() const {
		return cursor >= src.size();
	}

	bool Lexer::match(const char expected) {
		if (is_end() || src[cursor] != expected) return false;
		cursor++;
		col++;
		return true;
	}

	char Lexer::peek() const {
		return is_end() ? '\0' : src[cursor];
	}

	char Lexer::next() {
		cursor++;
		col++;
		return peek();
	}

	std::string_view Lexer::sub(const size_t start) const {
		return src.substr(start, cursor - start);
	}

	void Lexer::new_line() {
		switch (const auto p = peek()) {
			case '\r': {
				next();
				if (!is_end() && p == '\n') cursor++;
			}

			case '\n':
				cursor++;
				break;

			default:
				break;
		}

		line++;
		col = 1;
	}

	void Lexer::skip_space() {
		const auto c = peek();
		while (!is_end()) {
			switch (c) {
				case ' ':
				case '\t':
					next();
					break;

				case '\r':
				case '\n':
					new_line();
					break;

				default:
					break;
			}
		}
	}

}
