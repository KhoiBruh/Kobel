module;

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

module parser;

import ast;
import token;

Precedence Parser::get_infix_precedence(const TokenType type) const {
	switch (type) {
		case TokenType::EQUAL: return Precedence::ASSIGN;
		case TokenType::OR_OR: return Precedence::OR;
		case TokenType::AND_AND: return Precedence::AND;
		case TokenType::EQUAL_EQUAL:
		case TokenType::BANG_EQUAL: return Precedence::EQUALITY;
		case TokenType::LESS:
		case TokenType::LESS_EQUAL:
		case TokenType::GREATER:
		case TokenType::GREATER_EQUAL: return Precedence::COMPARISON;
		case TokenType::PLUS:
		case TokenType::MINUS: return Precedence::TERM;
		case TokenType::STAR:
		case TokenType::SLASH:
		case TokenType::PERCENT: return Precedence::FACTOR;
		case TokenType::KW_AS:
		case TokenType::OPEN_PAREN:
		case TokenType::DOT:
		case TokenType::OPEN_BRACKET: return Precedence::POSTFIX;
		default: return Precedence::NONE;
	}
}

std::unique_ptr<Expr> Parser::parse_prefix() {
	const auto tok = peek();

	// constant number
	if (match(TokenType::NUMBER)) {
		const bool is_float = tok.text.find('.') != std::string_view::npos;
		const LiteralKind kind = is_float ? LiteralKind::FLOAT : LiteralKind::INT;
		return std::make_unique<LiteralExpr>(kind, tok.text, tok.line, tok.col);
	}

	// constant string
	if (match(TokenType::STRING)) {
		return std::make_unique<LiteralExpr>(
			LiteralKind::STRING, tok.text, tok.line, tok.col
		);
	}

	// constant character
	if (match(TokenType::CHAR)) {
		return std::make_unique<LiteralExpr>(
			LiteralKind::CHAR, tok.text,
			tok.line, tok.col
		);
	}

	// Boolean / Null
	if (match(TokenType::KW_TRUE)) {
		return std::make_unique<LiteralExpr>(
			LiteralKind::BOOL, "true",
			tok.line, tok.col
		);
	}
	if (match(TokenType::KW_FALSE)) {
		return std::make_unique<LiteralExpr>(
			LiteralKind::BOOL, "false",
			tok.line, tok.col
		);
	}
	if (match(TokenType::KW_NULL)) {
		return std::make_unique<LiteralExpr>(
			LiteralKind::NULL_VAL, "null",
			tok.line, tok.col
		);
	}

	// name/identifier
	if (match(TokenType::IDENTIFIER)) {
		return std::make_unique<IdentifierExpr>(tok.text, tok.line, tok.col);
	}

	// single parentheses
	if (match(TokenType::OPEN_PAREN)) {
		auto expr = parse_expression();
		consume(TokenType::CLOSE_PAREN, "Expected ')' đóng biểu thức ngoặc");
		return std::make_unique<GroupExpr>(std::move(expr), tok.line, tok.col);
	}

	// single prefix operator: -x, !x, *ptr
	if (match(TokenType::MINUS) || match(TokenType::BANG) || match(TokenType::STAR)) {
		const auto op = previous();
		auto operand = parse_expression(Precedence::UNARY);
		return std::make_unique<UnaryExpr>(op.type, std::move(operand), op.line, op.col);
	}

	// Array literal: [expr1, expr2, ...]
	if (match(TokenType::OPEN_BRACKET)) {
		std::vector<std::unique_ptr<Expr>> elements;
		if (!check(TokenType::CLOSE_BRACKET)) {
			do {
				if (check(TokenType::CLOSE_BRACKET)) break;
				elements.push_back(parse_expression());
			} while (match(TokenType::COMMA));
		}
		consume(TokenType::CLOSE_BRACKET, "Expected ']' to end array literal");
		return std::make_unique<ArrayLiteralExpr>(std::move(elements), tok.line, tok.col);
	}

	error(tok, "Biểu thức không hợp lệ");
	advance();
	return nullptr;
}

std::unique_ptr<Expr> Parser::parse_expression(const Precedence min_prec) {
	auto left = parse_prefix();
	if (!left) return nullptr;

	while (!is_end() && min_prec < get_infix_precedence(peek().type)) {
		switch (const auto op = advance(); op.type) {
			// 1. function operator: callee(arg1, arg2, ...)
			case TokenType::OPEN_PAREN: {
				std::vector<std::unique_ptr<Expr>> args;
				if (!check(TokenType::CLOSE_PAREN)) {
					do {
						args.push_back(parse_expression());
					} while (match(TokenType::COMMA));
				}
				consume(TokenType::CLOSE_PAREN, "Expected ')' end danh sách đối số hàm");
				left = std::make_unique<CallExpr>(
					std::move(left), std::move(args),
					op.line, op.col
				);
				break;
			}
			// 2. access fields in struct: object.field
			case TokenType::DOT: {
				const auto member = consume(
					TokenType::IDENTIFIER,
					"Expected name trường after dấu '.'"
				);
				left = std::make_unique<MemberExpr>(
					std::move(left), member.text,
					op.line, op.col
				);
				break;
			}
			// 3. array/pointer index: ptr[index]
			case TokenType::OPEN_BRACKET: {
				auto index = parse_expression();
				consume(TokenType::CLOSE_BRACKET, "Expected ']' after chỉ mục");
				left = std::make_unique<IndexExpr>(
					std::move(left), std::move(index),
					op.line, op.col
				);
				break;
			}
			// 4. type cast: expr as Type
			case TokenType::KW_AS: {
				auto target_type = parse_type();
				left = std::make_unique<CastExpr>(
					std::move(left), std::move(target_type),
					op.line, op.col
				);
				break;
			}
			// 5. assign operator: target = value
			case TokenType::EQUAL: {
				auto value = parse_expression(Precedence::ASSIGN);
				left = std::make_unique<AssignExpr>(
					std::move(left), std::move(value),
					op.line, op.col
				);
				break;
			}
			// 6. normal binary operator: +, -, *, /, ==, !=, v.v.
			default: {
				const auto next_prec = get_infix_precedence(op.type);
				auto right = parse_expression(next_prec);
				left = std::make_unique<BinaryExpr>(
					std::move(left), op.type,
					std::move(right), op.line, op.col
				);
				break;
			}
		}
	}

	return left;
}
