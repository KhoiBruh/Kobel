module;

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module parser;

import token;
import ast;

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
	std::vector<std::string> errors;

	explicit Parser(std::vector<Token> toks) : tokens(std::move(toks)) {
	}

	bool is_end() const {
		return current >= tokens.size() || peek().type == TokenType::END_OF_FILE;
	}

	Token peek() const {
		if (current >= tokens.size()) return {TokenType::END_OF_FILE, "", 0, 0};
		return tokens[current];
	}

	Token previous() const {
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

	void error(const Token token, const std::string_view message) {
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
				case TokenType::KW_CONST:
				case TokenType::KW_EXTERN:
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

	std::unique_ptr<TypeNode> parse_type() {
		const Token tok = peek();

		// Pointer: *T
		if (match(TokenType::STAR)) {
			auto pointee = parse_type();
			return std::make_unique<PointerType>(
				false, std::move(pointee), tok.line, tok.col
			);
		}

		// single identifier: i32, u8, char, bool, MyStruct...
		if (match(TokenType::IDENTIFIER))
			return std::make_unique<NamedType>(
				previous().text, tok.line, tok.col
			);

		error(tok, "Expected name kiểu dữ liệu hoặc con trỏ '*'");
		return nullptr;
	}

	Precedence get_infix_precedence(const TokenType type) const {
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

	std::unique_ptr<Expr> parse_prefix() {
		const auto tok = peek();

		// constant number
		if (match(TokenType::NUMBER)) {
			const bool is_float = tok.text.find('.') != std::string_view::npos;
			const LiteralKind kind = is_float ? LiteralKind::FLOAT : LiteralKind::INT;
			return std::make_unique<LiteralExpr>(kind, tok.text, tok.line, tok.col);
		}

		// constant string
		if (match(TokenType::STRING))
			return std::make_unique<LiteralExpr>(
				LiteralKind::STRING, tok.text, tok.line, tok.col
			);

		// constant character
		if (match(TokenType::CHAR))
			return std::make_unique<LiteralExpr>(
				LiteralKind::CHAR, tok.text,
				tok.line, tok.col
			);

		// Boolean / Null
		if (match(TokenType::KW_TRUE))
			return std::make_unique<LiteralExpr>(
				LiteralKind::BOOL, "true",
				tok.line, tok.col
			);
		if (match(TokenType::KW_FALSE))
			return std::make_unique<LiteralExpr>(
				LiteralKind::BOOL, "false",
				tok.line, tok.col
			);
		if (match(TokenType::KW_NULL))
			return std::make_unique<LiteralExpr>(
				LiteralKind::NULL_VAL, "null",
				tok.line, tok.col
			);

		// name/identifier
		if (match(TokenType::IDENTIFIER)) return std::make_unique<IdentifierExpr>(tok.text, tok.line, tok.col);

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

		error(tok, "Biểu thức không hợp lệ");
		advance();
		return nullptr;
	}

	std::unique_ptr<Expr> parse_expression(const Precedence min_prec = Precedence::NONE) {
		auto left = parse_prefix();
		if (!left) return nullptr;

		while (!is_end() && min_prec < get_infix_precedence(peek().type)) {
			switch (const auto op = advance(); op.type) {
				// 1. function operator: callee(arg1, arg2, ...)
				case TokenType::OPEN_PAREN: {
					std::vector<std::unique_ptr<Expr> > args;
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

	std::unique_ptr<BlockStmt> parse_block_stmt() {
		const auto tok = consume(TokenType::OPEN_BRACE, "Expected '{' begin statement block");
		std::vector<std::unique_ptr<Stmt> > statements;

		while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
			if (auto stmt = parse_statement()) {
				statements.push_back(std::move(stmt));
			} else {
				synchronize();
			}
		}

		consume(TokenType::CLOSE_BRACE, "Expected '}' end statement block");
		return std::make_unique<BlockStmt>(std::move(statements), tok.line, tok.col);
	}

	std::unique_ptr<Stmt> parse_var_decl_stmt() {
		const auto tok = advance();
		const bool is_mut = tok.type == TokenType::KW_VAR;

		const auto name = consume(TokenType::IDENTIFIER, "Expected variable name after declaration");

		std::unique_ptr<TypeNode> type = nullptr;
		if (match(TokenType::COLON)) type = parse_type();

		std::unique_ptr<Expr> init = nullptr;
		if (match(TokenType::EQUAL)) init = parse_expression();

		consume(TokenType::SEMI_COLON, "Expected ';' end variable declatation");
		return std::make_unique<VarDeclStmt>(
			is_mut, name.text,
			std::move(type),
			std::move(init),
			tok.line, tok.col
		);
	}

	std::unique_ptr<Stmt> parse_if_stmt() {
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

	std::unique_ptr<Stmt> parse_while_stmt() {
		const Token tok = consume(TokenType::KW_WHILE, "Expected 'while'");
		consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'");
		auto cond = parse_expression();
		consume(TokenType::CLOSE_PAREN, "Expected ')' after while condition");

		auto body = parse_block_stmt();
		return std::make_unique<WhileStmt>(std::move(cond), std::move(body), tok.line, tok.col);
	}

	std::unique_ptr<Stmt> parse_return_stmt() {
		const Token tok = consume(TokenType::KW_RETURN, "Expected 'return'");
		std::unique_ptr<Expr> val = nullptr;

		if (!check(TokenType::SEMI_COLON)) val = parse_expression();

		consume(TokenType::SEMI_COLON, "Expected ';' after return statement");
		return std::make_unique<ReturnStmt>(std::move(val), tok.line, tok.col);
	}

	std::unique_ptr<Stmt> parse_statement() {
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
		consume(TokenType::SEMI_COLON, "Expected ';' end statement");
		return std::make_unique<ExprStmt>(std::move(expr), tok.line, tok.col);
	}

	std::unique_ptr<FnDecl> parse_fn_decl() {
		const auto tok = consume(TokenType::KW_FN, "Expected 'fn'");
		const auto name = consume(TokenType::IDENTIFIER, "Expected function name after 'fn'");

		consume(TokenType::OPEN_PAREN, "Expected '(' after function name");
		std::vector<Param> params;
		if (!check(TokenType::CLOSE_PAREN)) {
			do {
				const Token p_name = consume(TokenType::IDENTIFIER, "Expected parameter name");
				consume(TokenType::COLON, "Expected ':' after parameter name");
				auto p_type = parse_type();
				params.push_back(Param{p_name.text, std::move(p_type)});
			} while (match(TokenType::COMMA));
		}
		consume(TokenType::CLOSE_PAREN, "Expected ')' close parameter declarations");

		std::unique_ptr<TypeNode> ret_type = nullptr;
		if (match(TokenType::COLON)) ret_type = parse_type();

		std::unique_ptr<BlockStmt> body = nullptr;
		if (check(TokenType::OPEN_BRACE)) body = parse_block_stmt();
		else consume(
			TokenType::SEMI_COLON,
			"Expected function body '{' or ';' declare a prototype"
		);

		auto fn = std::make_unique<FnDecl>(name.text, tok.line, tok.col);
		fn->params = std::move(params);
		fn->return_type = std::move(ret_type);
		fn->body = std::move(body);
		return fn;
	}

	std::unique_ptr<StructDecl> parse_struct_decl() {
		const auto tok = consume(TokenType::KW_STRUCT, "Expected 'struct'");
		const auto name = consume(TokenType::IDENTIFIER, "Expected struct name");

		std::vector<StructField> fields;
		consume(TokenType::OPEN_PAREN, "Expected '(' for field declarations for struct");
		if (!check(TokenType::CLOSE_PAREN)) {
			do {
				const Token f_name = consume(TokenType::IDENTIFIER, "Expected field name");
				consume(TokenType::COLON, "Expected ':' after field name");
				auto f_type = parse_type();
				fields.push_back(StructField{f_name.text, std::move(f_type)});
			} while (match(TokenType::COMMA));
		}
		consume(TokenType::CLOSE_PAREN, "Expected ')' close field declarations for struct");

		// Hỗ trợ cả thân rỗng {} hoặc dấu chấm phẩy ;
		if (match(TokenType::OPEN_BRACE))
			consume(TokenType::CLOSE_BRACE, "Expected '}' end struct");
		else match(TokenType::SEMI_COLON);

		auto st = std::make_unique<StructDecl>(name.text, tok.line, tok.col);
		st->fields = std::move(fields);
		return st;
	}

	std::unique_ptr<ConstDecl> parse_const_decl() {
		const auto tok = consume(TokenType::KW_CONST, "Expected 'const'");
		const auto name = consume(TokenType::IDENTIFIER, "Expected constant name");

		consume(TokenType::COLON, "Expected ':' after constant name");
		auto type = parse_type();

		consume(TokenType::EQUAL, "Expected '=' to assign value for constant");
		auto val = parse_expression();

		consume(TokenType::SEMI_COLON, "Expected ';' end constant declaration");
		return std::make_unique<ConstDecl>(name.text, std::move(type), std::move(val), tok.line, tok.col);
	}

	std::unique_ptr<ExternBlock> parse_extern_block() {
		const auto tok = consume(TokenType::KW_EXTERN, "Expected 'extern'");
		const auto abi = consume(TokenType::STRING, "Expected ABI string (vd: \"libc\", \"C\") after 'extern'");

		consume(TokenType::OPEN_BRACE, "Expected '{' begin extern block");
		std::vector<std::unique_ptr<FnDecl> > declarations;

		while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
			if (check(TokenType::KW_FN)) declarations.push_back(parse_fn_decl());
			else advance();
		}

		consume(TokenType::CLOSE_BRACE, "Expected '}' end extern block");
		auto ext = std::make_unique<ExternBlock>(abi.text, tok.line, tok.col);
		ext->declarations = std::move(declarations);
		return ext;
	}

	std::unique_ptr<Decl> parse_declaration() {
		if (check(TokenType::KW_FN)) return parse_fn_decl();
		if (check(TokenType::KW_STRUCT)) return parse_struct_decl();
		if (check(TokenType::KW_CONST)) return parse_const_decl();
		if (check(TokenType::KW_EXTERN)) return parse_extern_block();

		error(peek(), "Expected top-level declaration ('fn', 'struct', 'const', 'extern')");
		advance();
		return nullptr;
	}

	std::unique_ptr<Program> parse_program() {
		auto program = std::make_unique<Program>();

		while (!is_end()) {
			if (auto decl = parse_declaration())
				program->declarations.push_back(std::move(decl));
			else synchronize();
		}

		return program;
	}
};
