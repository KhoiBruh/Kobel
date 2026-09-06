module;

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module parser;

export import token;
export import ast;

export enum class Precedence {
	NONE,
	ASSIGN,     // =
	OR,         // ||
	AND,        // &&
	EQUALITY,   // == !=
	COMPARISON, // < <= > >=
	TERM,       // + -
	FACTOR,     // * / %
	UNARY,      // ! - *
	POSTFIX     // () . [] as
};

export struct Parser {
	std::vector<Token> tokens;
	size_t current = 0;
	std::vector<std::string> errors;

	explicit Parser(std::vector<Token> toks) : tokens(std::move(toks)) {}

	// 1. Navigation & Helper methods
	bool is_end() const {
		return current >= tokens.size() || peek().type == TokenType::END_OF_FILE;
	}

	Token peek() const {
		if (current >= tokens.size()) return {TokenType::END_OF_FILE, "", 0, 0};
		return tokens[current];
	}

	Token previous() const {
		if (tokens.empty()) return {TokenType::END_OF_FILE, "", 0, 0};
		return current > 0 ? tokens[current - 1] : tokens[0];
	}

	Token advance() {
		if (!is_end()) current++;
		return previous();
	}

	bool check(const TokenType type) const {
		if (is_end()) return false;
		return peek().type == type;
	}

	bool match(const TokenType type) {
		if (check(type)) {
			advance();
			return true;
		}
		return false;
	}

	Token consume(const TokenType type, const std::string_view message) {
		if (check(type)) return advance();
		error(peek(), message);
		return peek();
	}

	void error(const Token &token, const std::string_view message) {
		std::string err = "[Syntax error] Line " + std::to_string(token.line) +
		                  ", Column " + std::to_string(token.col) + ": " +
		                  std::string(message) + " (encounter '" + std::string(token.text) + "')";
		errors.push_back(std::move(err));
	}

	bool has_errors() const {
		return !errors.empty();
	}

	void synchronize() {
		advance();
		while (!is_end()) {
			if (previous().type == TokenType::SEMI_COLON) return;

			switch (peek().type) {
				case TokenType::KW_FN:
				case TokenType::KW_STRUCT:
				case TokenType::KW_ENUM:
				case TokenType::KW_CONST:
				case TokenType::KW_EXTERN:
				case TokenType::KW_MODULE:
				case TokenType::KW_USE:
				case TokenType::KW_PUB:
				case TokenType::KW_VAL:
				case TokenType::KW_VAR:
				case TokenType::KW_IF:
				case TokenType::KW_WHILE:
				case TokenType::KW_RETURN:
					return;
				default:
					break;
			}
			advance();
		}
	}

	// 2. Precedence (ParserExpr.cpp)
	Precedence get_infix_precedence(TokenType type) const;

	// 3. Type parsing (ParserType.cpp)
	std::unique_ptr<TypeNode> parse_type();

	// 4. Expression parsing (ParserExpr.cpp)
	std::unique_ptr<Expr> parse_prefix();
	std::unique_ptr<Expr> parse_expression(Precedence min_prec = Precedence::NONE);

	// 5. Statement parsing (ParserStmt.cpp)
	std::unique_ptr<BlockStmt> parse_block_stmt();
	std::unique_ptr<Stmt> parse_var_decl_stmt();
	std::unique_ptr<Stmt> parse_if_stmt();
	std::unique_ptr<Stmt> parse_while_stmt();
	std::unique_ptr<Stmt> parse_return_stmt();
	std::unique_ptr<Stmt> parse_statement();

	// 6. Declaration parsing (ParserDecl.cpp)
	std::unique_ptr<ModuleDecl> parse_module_decl();
	std::unique_ptr<UseDecl> parse_use_decl();
	std::unique_ptr<FnDecl> parse_fn_decl();
	std::unique_ptr<StructDecl> parse_struct_decl();
	std::unique_ptr<EnumDecl> parse_enum_decl();
	std::unique_ptr<ConstDecl> parse_const_decl();
	std::unique_ptr<ExternBlock> parse_extern_block();
	std::unique_ptr<Decl> parse_declaration();
	std::unique_ptr<Program> parse_program();
};
