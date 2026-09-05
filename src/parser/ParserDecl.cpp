module;

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

module parser;

import ast;
import token;

std::unique_ptr<FnDecl> Parser::parse_fn_decl() {
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
	if (check(TokenType::OPEN_BRACE))body = parse_block_stmt();
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

std::unique_ptr<StructDecl> Parser::parse_struct_decl() {
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

std::unique_ptr<EnumDecl> Parser::parse_enum_decl() {
	const auto tok = consume(TokenType::KW_ENUM, "Expected 'enum'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected enum name");

	std::unique_ptr<TypeNode> underlying = nullptr;
	if (match(TokenType::COLON)) {
		underlying = parse_type();
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin enum body");

	std::vector<EnumMember> members;
	if (!check(TokenType::CLOSE_BRACE)) {
		do {
			if (check(TokenType::CLOSE_BRACE)) break;
			const Token m_name = consume(TokenType::IDENTIFIER, "Expected enum member name");
			std::unique_ptr<Expr> val = nullptr;
			if (match(TokenType::EQUAL)) {
				val = parse_expression();
			}
			members.push_back(EnumMember{m_name.text, std::move(val), m_name.line, m_name.col});
		} while (match(TokenType::COMMA));
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end enum body");

	auto enum_decl = std::make_unique<EnumDecl>(name.text, tok.line, tok.col);
	enum_decl->underlying_type = std::move(underlying);
	enum_decl->members = std::move(members);
	return enum_decl;
}

std::unique_ptr<ConstDecl> Parser::parse_const_decl() {
	const auto tok = consume(TokenType::KW_CONST, "Expected 'const'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected constant name");

	consume(TokenType::COLON, "Expected ':' after constant name");
	auto type = parse_type();

	consume(TokenType::EQUAL, "Expected '=' to assign value for constant");
	auto val = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' end constant declaration");
	return std::make_unique<ConstDecl>(name.text, std::move(type), std::move(val), tok.line, tok.col);
}

std::unique_ptr<ExternBlock> Parser::parse_extern_block() {
	const auto tok = consume(TokenType::KW_EXTERN, "Expected 'extern'");
	const auto abi = consume(TokenType::STRING, "Expected ABI string (vd: \"libc\", \"C\") after 'extern'");

	consume(TokenType::OPEN_BRACE, "Expected '{' begin extern block");
	std::vector<std::unique_ptr<FnDecl>> declarations;

	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		if (check(TokenType::KW_FN))
			declarations.push_back(parse_fn_decl());
		else advance();
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' end extern block");
	auto ext = std::make_unique<ExternBlock>(abi.text, tok.line, tok.col);
	ext->declarations = std::move(declarations);
	return ext;
}

std::unique_ptr<Decl> Parser::parse_declaration() {
	if (check(TokenType::KW_FN)) return parse_fn_decl();
	if (check(TokenType::KW_STRUCT)) return parse_struct_decl();
	if (check(TokenType::KW_ENUM)) return parse_enum_decl();
	if (check(TokenType::KW_CONST)) return parse_const_decl();
	if (check(TokenType::KW_EXTERN)) return parse_extern_block();

	error(peek(), "Expected top-level declaration ('fn', 'struct', 'enum', 'const', 'extern')");
	advance();
	return nullptr;
}

std::unique_ptr<Program> Parser::parse_program() {
	auto program = std::make_unique<Program>();

	while (!is_end()) {
		if (auto decl = parse_declaration())
			program->declarations.push_back(std::move(decl));
		else synchronize();
	}

	return program;
}
