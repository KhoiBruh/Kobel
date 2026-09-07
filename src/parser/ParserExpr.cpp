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

Expr* Parser::parse_prefix() {
	const auto tok = peek();

	// constant number
	if (match(TokenType::NUMBER)) {
		const bool is_float = tok.text.find('.') != std::string_view::npos;
		const LiteralKind kind = is_float ? LiteralKind::FLOAT : LiteralKind::INT;
		return arena.alloc<LiteralExpr>(kind, tok.text, tok.line, tok.col);
	}

	// constant string
	if (match(TokenType::STRING)) {
		return arena.alloc<LiteralExpr>(
			LiteralKind::STRING, tok.text, tok.line, tok.col
		);
	}

	// constant character
	if (match(TokenType::CHAR)) {
		return arena.alloc<LiteralExpr>(
			LiteralKind::CHAR, tok.text,
			tok.line, tok.col
		);
	}

	// Boolean / Null
	if (match(TokenType::KW_TRUE)) {
		return arena.alloc<LiteralExpr>(
			LiteralKind::BOOL, "true",
			tok.line, tok.col
		);
	}
	if (match(TokenType::KW_FALSE)) {
		return arena.alloc<LiteralExpr>(
			LiteralKind::BOOL, "false",
			tok.line, tok.col
		);
	}
	if (match(TokenType::KW_NULL)) {
		return arena.alloc<LiteralExpr>(
			LiteralKind::NULL_VAL, "null",
			tok.line, tok.col
		);
	}

	// name/identifier
	if (match(TokenType::IDENTIFIER)) {
		return arena.alloc<IdentifierExpr>(tok.text, tok.line, tok.col);
	}

	// single parentheses
	if (match(TokenType::OPEN_PAREN)) {
		auto expr = parse_expression();
		consume(TokenType::CLOSE_PAREN, "Expected ')' to close grouped expression");
		return arena.alloc<GroupExpr>(expr, tok.line, tok.col);
	}

	// single prefix operator: -x, !x, *ptr
	if (match(TokenType::MINUS) || match(TokenType::BANG) || match(TokenType::STAR)) {
		const auto op = previous();
		auto operand = parse_expression(Precedence::UNARY);
		return arena.alloc<UnaryExpr>(op.type, operand, op.line, op.col);
	}

	// Array literal: [expr1, expr2, ...]
	if (match(TokenType::OPEN_BRACKET)) {
		std::vector<Expr*> elements;
		if (!check(TokenType::CLOSE_BRACKET)) {
			do {
				if (check(TokenType::CLOSE_BRACKET)) break;
				elements.push_back(parse_expression());
			} while (match(TokenType::COMMA));
		}
		consume(TokenType::CLOSE_BRACKET, "Expected ']' to end array literal");
		return arena.alloc<ArrayLiteralExpr>(arena.alloc_span(elements), tok.line, tok.col);
	}

	// if expression: if (cond) then_expr else else_expr
	if (match(TokenType::KW_IF)) {
		return parse_if_expr();
	}

	// when expression: when (cond) { ... }
	if (match(TokenType::KW_WHEN)) {
		return parse_when_expr();
	}

	if (tok.type == TokenType::UNKNOWN && tok.text.starts_with('"')) {
		error(tok, "Unterminated string literal");
		advance();
		return nullptr;
	}

	error(tok, "Invalid expression");
	advance();
	return nullptr;
}

Expr* Parser::parse_if_expr() {
	const Token tok = previous(); // KW_IF
	consume(TokenType::OPEN_PAREN, "Expected '(' after 'if'");
	auto cond = parse_expression();
	consume(TokenType::CLOSE_PAREN, "Expected ')' after if condition");

	Expr* then_branch = nullptr;
	if (match(TokenType::OPEN_BRACE)) {
		then_branch = parse_expression();
		match(TokenType::SEMI_COLON);
		consume(TokenType::CLOSE_BRACE, "Expected '}' after then block in if-expression");
	} else {
		then_branch = parse_expression();
	}

	consume(TokenType::KW_ELSE, "Expected 'else' in if-expression");

	Expr* else_branch = nullptr;
	if (match(TokenType::OPEN_BRACE)) {
		else_branch = parse_expression();
		match(TokenType::SEMI_COLON);
		consume(TokenType::CLOSE_BRACE, "Expected '}' after else block in if-expression");
	} else {
		else_branch = parse_expression();
	}

	return arena.alloc<IfExpr>(cond, then_branch, else_branch, tok.line, tok.col);
}

Expr* Parser::parse_when_expr() {
	const Token tok = previous(); // KW_WHEN

	Expr* condition = nullptr;
	if (match(TokenType::OPEN_PAREN)) {
		condition = parse_expression();
		consume(TokenType::CLOSE_PAREN, "Expected ')' after when condition");
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to start when block");

	std::vector<WhenArm> arms;
	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		WhenArm arm;
		arm.line = peek().line;
		arm.col = peek().col;

		if (match(TokenType::KW_ELSE)) {
			arm.is_else = true;
		} else {
			std::vector<Expr*> patterns;
			patterns.push_back(parse_expression());
			while (match(TokenType::COMMA)) {
				patterns.push_back(parse_expression());
			}
			arm.patterns = arena.alloc_span<Expr*>(patterns);
		}

		consume(TokenType::ARROW, "Expected '->' after when pattern");

		if (match(TokenType::OPEN_BRACE)) {
			arm.body = parse_expression();
			match(TokenType::SEMI_COLON);
			consume(TokenType::CLOSE_BRACE, "Expected '}' after arm block in when-expression");
			if (!match(TokenType::SEMI_COLON)) {
				match(TokenType::COMMA);
			}
		} else {
			arm.body = parse_expression();
			if (!match(TokenType::SEMI_COLON) && !match(TokenType::COMMA)) {
				if (!check(TokenType::CLOSE_BRACE)) {
					consume(TokenType::SEMI_COLON, "Expected ';' or ',' after when arm expression");
				}
			}
		}

		arms.push_back(arm);
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to close when block");
	return arena.alloc<WhenExpr>(condition, arena.alloc_span<WhenArm>(arms), tok.line, tok.col);
}

Expr* Parser::parse_expression(const Precedence min_prec) {
	auto left = parse_prefix();
	if (!left) return nullptr;

	while (!is_end() && min_prec < get_infix_precedence(peek().type)) {
		switch (const auto op = advance(); op.type) {
			// 1. function operator: callee(arg1, arg2, ...)
			case TokenType::OPEN_PAREN: {
				std::vector<Expr*> args;
				if (!check(TokenType::CLOSE_PAREN)) {
					do {
						args.push_back(parse_expression());
					} while (match(TokenType::COMMA));
				}
				consume(TokenType::CLOSE_PAREN, "Expected ')' after argument list");
				left = arena.alloc<CallExpr>(
					left, arena.alloc_span(args),
					op.line, op.col
				);
				break;
			}
			// 2. access fields in struct: object.field
			case TokenType::DOT: {
				const auto member = consume(
					TokenType::IDENTIFIER,
					"Expected member name after '.'"
				);
				left = arena.alloc<MemberExpr>(
					left, member.text,
					op.line, op.col
				);
				break;
			}
			// 3. array/pointer index: ptr[index]
			case TokenType::OPEN_BRACKET: {
				auto index = parse_expression();
				consume(TokenType::CLOSE_BRACKET, "Expected ']' after index expression");
				left = arena.alloc<IndexExpr>(
					left, index,
					op.line, op.col
				);
				break;
			}
			// 4. type cast: expr as Type
			case TokenType::KW_AS: {
				auto target_type = parse_type();
				left = arena.alloc<CastExpr>(
					left, target_type,
					op.line, op.col
				);
				break;
			}
			// 5. assign operator: target = value
			case TokenType::EQUAL: {
				auto value = parse_expression(Precedence::NONE);
				left = arena.alloc<AssignExpr>(
					left, value,
					op.line, op.col
				);
				break;
			}
			// 6. normal binary operator: +, -, *, /, ==, !=, v.v.
			default: {
				const auto next_prec = get_infix_precedence(op.type);
				auto right = parse_expression(next_prec);
				left = arena.alloc<BinaryExpr>(
					left, op.type,
					right, op.line, op.col
				);
				break;
			}
		}
	}

	return left;
}



