module;

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

module parser;

import ast;
import token;

std::unique_ptr<BlockStmt> Parser::parse_block_stmt() {
	const auto tok = consume(TokenType::OPEN_BRACE, "Expected '{' to begin statement block");
	std::vector<std::unique_ptr<Stmt>> statements;

	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		if (auto stmt = parse_statement())
			statements.push_back(std::move(stmt));
		else synchronize();
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end statement block");
	return std::make_unique<BlockStmt>(std::move(statements), tok.line, tok.col);
}

std::unique_ptr<Stmt> Parser::parse_var_decl_stmt() {
	const auto tok = advance();
	const bool is_mut = tok.type == TokenType::KW_VAR;

	const auto name = consume(TokenType::IDENTIFIER, "Expected variable name after declaration");

	std::unique_ptr<TypeNode> type = nullptr;
	if (match(TokenType::COLON)) type = parse_type();

	std::unique_ptr<Expr> init = nullptr;
	if (match(TokenType::EQUAL)) init = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after variable declaration");
	return std::make_unique<VarDeclStmt>(
		is_mut, name.text,
		std::move(type),
		std::move(init),
		tok.line, tok.col
	);
}

std::unique_ptr<Stmt> Parser::parse_if_stmt() {
	const auto tok = consume(TokenType::KW_IF, "Expected 'if'");
	consume(TokenType::OPEN_PAREN, "Expected '(' after 'if'");
	auto cond = parse_expression();
	consume(TokenType::CLOSE_PAREN, "Expected ')' after if condition");

	auto then_branch = parse_block_stmt();

	std::unique_ptr<Stmt> else_branch = nullptr;
	if (match(TokenType::KW_ELSE)) {
		if (check(TokenType::KW_IF)) else_branch = parse_if_stmt();
		else else_branch = parse_block_stmt();
	}

	return std::make_unique<IfStmt>(
		std::move(cond),
		std::move(then_branch),
		std::move(else_branch),
		tok.line, tok.col
	);
}

std::unique_ptr<Stmt> Parser::parse_while_stmt() {
	const Token tok = consume(TokenType::KW_WHILE, "Expected 'while'");
	consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'");
	auto cond = parse_expression();
	consume(TokenType::CLOSE_PAREN, "Expected ')' after while condition");

	auto body = parse_block_stmt();
	return std::make_unique<WhileStmt>(std::move(cond), std::move(body), tok.line, tok.col);
}

std::unique_ptr<Stmt> Parser::parse_return_stmt() {
	const Token tok = consume(TokenType::KW_RETURN, "Expected 'return'");
	std::unique_ptr<Expr> val = nullptr;

	if (!check(TokenType::SEMI_COLON)) val = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after return statement");
	return std::make_unique<ReturnStmt>(std::move(val), tok.line, tok.col);
}

std::unique_ptr<Stmt> Parser::parse_statement() {
	if (check(TokenType::KW_VAL) || check(TokenType::KW_VAR)) return parse_var_decl_stmt();
	if (check(TokenType::KW_IF)) return parse_if_stmt();
	if (check(TokenType::KW_WHILE)) return parse_while_stmt();
	if (check(TokenType::KW_RETURN)) return parse_return_stmt();
	if (match(TokenType::KW_BREAK)) {
		const Token tok = previous();
		consume(TokenType::SEMI_COLON, "Expected ';' after 'break'");
		return std::make_unique<BreakStmt>(tok.line, tok.col);
	}
	if (match(TokenType::KW_CONTINUE)) {
		const auto tok = previous();
		consume(TokenType::SEMI_COLON, "Expected ';' after 'continue'");
		return std::make_unique<ContinueStmt>(tok.line, tok.col);
	}
	if (check(TokenType::OPEN_BRACE)) return parse_block_stmt();

	// Expression also is a statement (ex: printf(...); or x = 10;)
	const auto tok = peek();
	auto expr = parse_expression();
	consume(TokenType::SEMI_COLON, "Expected ';' after statement");
	return std::make_unique<ExprStmt>(std::move(expr), tok.line, tok.col);
}
