module;

#include <memory>
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

	// single identifier: i32, u8, char, bool, MyStruct...
	if (match(TokenType::IDENTIFIER)) {
		return std::make_unique<NamedType>(
			previous().text, tok.line, tok.col
		);
	}

	error(tok, "Expected name kiểu dữ liệu hoặc con trỏ '*'");
	return nullptr;
}
