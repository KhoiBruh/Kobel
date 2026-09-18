module;

#include <charconv>
#include <memory>
#include <string>
#include <string_view>

module parser;

import ast;
import token;

TypeNode *Parser::parse_type() {
	const Token tok = peek();

	// Pointer: *T (read-only) or &T (read-write)
	if (match(TokenType::STAR)) {
		auto pointee = parse_type();
		return arena.alloc<PointerType>(
			false, pointee, tok.line, tok.col
		);
	}
	if (match(TokenType::AMPERSAND)) {
		auto pointee = parse_type();
		return arena.alloc<PointerType>(
			true, pointee, tok.line, tok.col
		);
	}

	// Array: Array<T> or Array<T>(N)
	if (check(TokenType::IDENTIFIER) && peek().text == "Array") {
		const Token arr_tok = advance();
		consume(TokenType::LESS, "Expected '<' after 'Array'");
		auto elem_type = parse_type();
		consume(TokenType::GREATER, "Expected '>' after array element type");

		size_t explicit_size = 0;
		if (match(TokenType::OPEN_PAREN)) {
			const auto size_tok = consume(TokenType::NUMBER, "Expected array size in parentheses");
			std::string s;
			s.reserve(size_tok.text.size());
			for (const char c : size_tok.text) {
				if (c != '_') s.push_back(c);
			}
			int base = 10;
			size_t start = 0;
			if (s.starts_with("0x") || s.starts_with("0X")) {
				base = 16;
				start = 2;
			} else if (s.starts_with("0b") || s.starts_with("0B")) {
				base = 2;
				start = 2;
			} else if (s.starts_with("0o") || s.starts_with("0O")) {
				base = 8;
				start = 2;
			}
			while (!s.empty() && (s.back() == 'U' || s.back() == 'L' || s.back() == 'S' || s.back() == 'B' || s.back() == 'Z')) {
				s.pop_back();
			}
			const char *begin = s.data() + start;
			const char *end = s.data() + s.size();
			if (begin >= end) {
				error(size_tok, "Invalid array size number");
			} else {
				auto [ptr, ec] = std::from_chars(begin, end, explicit_size, base);
				if (ec != std::errc{} || ptr != end) {
					error(size_tok, "Invalid array size number");
				}
			}
			consume(TokenType::CLOSE_PAREN, "Expected ')' after array size");
		}

		return arena.alloc<ArrayType>(elem_type, explicit_size, arr_tok.line, arr_tok.col);
	}

	// identifier: i32, u8, char, bool, MyStruct... or generic type: Box<i32>, Pair<i32, str> or module-prefixed b.B
	if (match(TokenType::IDENTIFIER)) {
		const Token id_tok = previous();
		std::string full_name(id_tok.text);
		while (match(TokenType::DOT)) {
			const auto seg = consume(TokenType::IDENTIFIER, "Expected identifier after '.' in type name");
			full_name += ".";
			full_name += seg.text;
		}

		std::vector<TypeNode *> type_args;
		if (match(TokenType::LESS)) {
			do {
				type_args.push_back(parse_type());
			} while (match(TokenType::COMMA));
			consume(TokenType::GREATER, "Expected '>' after generic type arguments");
		}
		return arena.alloc<NamedType>(
			arena.alloc_string(full_name), arena.alloc_span(type_args), id_tok.line, id_tok.col
		);
	}

	error(tok, "Expected type name or pointer ('*' or '&')");
	return nullptr;
}
