module;

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module parser;

export import token;
export import ast;
export import logger;

export enum class Precedence {
	NONE,
	ASSIGN, // =
	OR, // ||
	AND, // &&
	EQUALITY, // == !=
	COMPARISON, // < <= > >=
	TERM, // + -
	FACTOR, // * / %
	UNARY, // ! - *
	POSTFIX // () . [] as
};

export struct Parser {
	std::vector<Token> tokens;
	size_t current = 0;
	DiagnosticEngine *diag = nullptr;
	DiagnosticEngine local_diag;
	std::vector<std::string> errors;
	Arena arena;

	explicit Parser(std::vector<Token> toks, DiagnosticEngine *d = nullptr)
		: tokens(std::move(toks)), diag(d) {
	}

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
		DiagnosticEngine &d = diag ? *diag : local_diag;
		auto msg = std::string(message);
		if (token.type != TokenType::END_OF_FILE && !token.text.empty()) {
			msg += " (found '" + std::string(token.text) + "')";
		}
		d.error(token.line, token.col, msg);
		errors.push_back("Line " + std::to_string(token.line) + ", Col " + std::to_string(token.col) + ": " + msg);
	}

	bool has_errors() const {
		return diag ? diag->has_errors() : local_diag.has_errors();
	}

	const std::vector<Diagnostic> &get_diagnostics() const {
		return diag ? diag->diagnostics : local_diag.diagnostics;
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
				case TokenType::KW_WHEN:
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
	TypeNode *parse_type();

	// 4. Expression parsing (ParserExpr.cpp)
	Expr *parse_prefix();

	Expr *parse_expression(Precedence min_prec = Precedence::NONE);

	Expr *parse_if_expr();

	Expr *parse_when_expr();

	// 5. Statement parsing (ParserStmt.cpp)
	BlockStmt *parse_block_stmt();

	Stmt *parse_var_decl_stmt();

	Stmt *parse_if_stmt();

	Stmt *parse_when_stmt();

	Stmt *parse_while_stmt();

	Stmt *parse_return_stmt();

	Stmt *parse_statement();

	// 6. Declaration parsing (ParserDecl.cpp)
	ModuleDecl *parse_module_decl();

	UseDecl *parse_use_decl();

	FnDecl *parse_fn_decl();

	StructDecl *parse_struct_decl();

	EnumDecl *parse_enum_decl();

	ConstDecl *parse_const_decl();

	ExternBlock *parse_extern_block();

	Decl *parse_declaration();

	Program *parse_program();
};
