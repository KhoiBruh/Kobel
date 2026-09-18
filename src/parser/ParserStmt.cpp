module;

#include <string_view>
#include <vector>

module parser;

import ast;
import token;

BlockStmt *Parser::parse_block_stmt() {
	const auto tok = consume(TokenType::OPEN_BRACE, "Expected '{' to begin statement block");
	std::vector<Stmt *> statements;

	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		if (auto stmt = parse_statement())
			statements.push_back(stmt);
		else synchronize();
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end statement block");
	return arena.alloc<BlockStmt>(arena.alloc_span(statements), tok.line, tok.col);
}

Stmt *Parser::parse_var_decl_stmt() {
	const auto tok = advance();
	const bool is_mut = tok.type == TokenType::KW_VAR;

	const auto name = consume(TokenType::IDENTIFIER, "Expected variable name after declaration");

	TypeNode *type = nullptr;
	if (match(TokenType::COLON)) type = parse_type();

	Expr *init = nullptr;
	if (match(TokenType::EQUAL)) init = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after variable declaration");
	return arena.alloc<VarDeclStmt>(
		is_mut, name.text,
		type,
		init,
		tok.line, tok.col
	);
}

Stmt *Parser::parse_if_stmt() {
	const auto tok = consume(TokenType::KW_IF, "Expected 'if'");
	consume(TokenType::OPEN_PAREN, "Expected '(' after 'if'");
	auto cond = parse_expression();
	consume(TokenType::CLOSE_PAREN, "Expected ')' after if condition");

	BlockStmt *then_branch = nullptr;
	if (check(TokenType::OPEN_BRACE)) {
		then_branch = parse_block_stmt();
	} else {
		auto single_stmt = parse_statement();
		if (single_stmt) {
			then_branch = arena.alloc<BlockStmt>(arena.alloc_span<Stmt *>({single_stmt}), single_stmt->line, single_stmt->col);
		}
	}

	Stmt *else_branch = nullptr;
	if (match(TokenType::KW_ELSE)) {
		if (check(TokenType::KW_IF)) {
			else_branch = parse_if_stmt();
		} else if (check(TokenType::OPEN_BRACE)) {
			else_branch = parse_block_stmt();
		} else {
			auto single_stmt = parse_statement();
			if (single_stmt) {
				else_branch = arena.alloc<BlockStmt>(
					arena.alloc_span<Stmt *>({single_stmt}), single_stmt->line, single_stmt->col
				);
			}
		}
	}

	return arena.alloc<IfStmt>(
		cond,
		then_branch,
		else_branch,
		tok.line, tok.col
	);
}

Stmt *Parser::parse_when_stmt() {
	const Token tok = consume(TokenType::KW_WHEN, "Expected 'when'");

	Expr *condition = nullptr;
	if (match(TokenType::OPEN_PAREN)) {
		condition = parse_expression();
		consume(TokenType::CLOSE_PAREN, "Expected ')' after when condition");
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to start when block");

	std::vector<WhenStmtArm> arms;
	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		WhenStmtArm arm;
		arm.line = peek().line;
		arm.col = peek().col;

		if (match(TokenType::KW_ELSE)) {
			arm.is_else = true;
		} else {
			std::vector<Expr *> patterns;
			patterns.push_back(parse_expression());
			while (match(TokenType::COMMA)) {
				patterns.push_back(parse_expression());
			}
			arm.patterns = arena.alloc_span<Expr *>(patterns);
		}

		consume(TokenType::ARROW, "Expected '->' after when pattern");

		if (check(TokenType::OPEN_BRACE)) {
			arm.body = parse_block_stmt();
			match(TokenType::SEMI_COLON); // optional semicolon after block
		} else {
			if (check(TokenType::KW_RETURN) || check(TokenType::KW_IF) || check(TokenType::KW_WHILE) ||
			    check(TokenType::KW_VAL) || check(TokenType::KW_VAR) || check(TokenType::KW_WHEN)) {
				arm.body = parse_statement();
			} else {
				auto expr = parse_expression();
				if (!match(TokenType::SEMI_COLON) && !match(TokenType::COMMA)) {
					if (!check(TokenType::CLOSE_BRACE)) {
						consume(TokenType::SEMI_COLON, "Expected ';' after when arm expression");
					}
				}
				arm.body = arena.alloc<ExprStmt>(expr, expr ? expr->line : arm.line, expr ? expr->col : arm.col);
			}
		}

		arms.push_back(arm);
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to close when block");
	return arena.alloc<WhenStmt>(condition, arena.alloc_span<WhenStmtArm>(arms), tok.line, tok.col);
}

Stmt *Parser::parse_while_stmt() {
	const Token tok = consume(TokenType::KW_WHILE, "Expected 'while'");
	consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'");
	auto cond = parse_expression();
	consume(TokenType::CLOSE_PAREN, "Expected ')' after while condition");

	auto body = parse_block_stmt();
	return arena.alloc<WhileStmt>(cond, body, tok.line, tok.col);
}

Stmt *Parser::parse_return_stmt() {
	const Token tok = consume(TokenType::KW_RETURN, "Expected 'return'");
	Expr *val = nullptr;

	if (!check(TokenType::SEMI_COLON)) val = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after return statement");
	return arena.alloc<ReturnStmt>(val, tok.line, tok.col);
}

Stmt *Parser::parse_statement() {
	if (check(TokenType::KW_VAL) || check(TokenType::KW_VAR)) return parse_var_decl_stmt();
	if (check(TokenType::KW_IF)) return parse_if_stmt();
	if (check(TokenType::KW_WHEN)) return parse_when_stmt();
	if (check(TokenType::KW_WHILE)) return parse_while_stmt();
	if (check(TokenType::KW_RETURN)) return parse_return_stmt();
	if (match(TokenType::KW_BREAK)) {
		const Token tok = previous();
		consume(TokenType::SEMI_COLON, "Expected ';' after 'break'");
		return arena.alloc<BreakStmt>(tok.line, tok.col);
	}
	if (match(TokenType::KW_CONTINUE)) {
		const auto tok = previous();
		consume(TokenType::SEMI_COLON, "Expected ';' after 'continue'");
		return arena.alloc<ContinueStmt>(tok.line, tok.col);
	}
	if (check(TokenType::OPEN_BRACE)) return parse_block_stmt();

	// Expression also is a statement (ex: printf(...); or x = 10;)
	const auto tok = peek();
	auto expr = parse_expression();
	consume(TokenType::SEMI_COLON, "Expected ';' after statement");
	return arena.alloc<ExprStmt>(expr, tok.line, tok.col);
}
