module;

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
			try {
				explicit_size = std::stoull(std::string(size_tok.text));
			} catch (...) {
				error(size_tok, "Invalid array size number");
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
