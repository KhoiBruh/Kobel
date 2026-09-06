module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>

module parser;

import ast;
import token;

std::unique_ptr<TypeNode> Parser::parse_type() {
	const Token tok = peek();

	// Pointer: *T
	if (match(TokenType::STAR)) {
		auto pointee = parse_type();
		return std::make_unique<PointerType>(
			false, std::move(pointee), tok.line, tok.col
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

		return std::make_unique<ArrayType>(std::move(elem_type), explicit_size, arr_tok.line, arr_tok.col);
	}

	// single identifier: i32, u8, char, bool, MyStruct...
	if (match(TokenType::IDENTIFIER)) {
		return std::make_unique<NamedType>(
			previous().text, tok.line, tok.col
		);
	}

	error(tok, "Expected type name or pointer '*'");
	return nullptr;
}
