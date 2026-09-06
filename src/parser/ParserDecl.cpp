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
			bool is_mut = false;
			bool has_val = false;
			if (match(TokenType::KW_VAR)) {
				is_mut = true;
			} else if (match(TokenType::KW_VAL)) {
				has_val = true;
			}

			const Token p_name = consume(TokenType::IDENTIFIER, "Expected parameter name");
			std::unique_ptr<TypeNode> p_type = nullptr;
			if (match(TokenType::COLON)) {
				p_type = parse_type();
			} else if (p_name.text != "self") {
				error(p_name, "Expected ':' and type after parameter name");
			}

			params.push_back(Param{p_name.text, std::move(p_type), is_mut, has_val});
		} while (match(TokenType::COMMA));
	}
	consume(TokenType::CLOSE_PAREN, "Expected ')' to close parameter list");

	std::unique_ptr<TypeNode> ret_type = nullptr;
	if (match(TokenType::COLON)) ret_type = parse_type();

	std::unique_ptr<BlockStmt> body = nullptr;
	if (check(TokenType::OPEN_BRACE)) {
		body = parse_block_stmt();
	} else if (match(TokenType::FAT_ARROW)) {
		auto expr = parse_expression();
		consume(TokenType::SEMI_COLON, "Expected ';' after expression body");
		std::vector<std::unique_ptr<Stmt>> stmts;
		stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr), tok.line, tok.col));
		body = std::make_unique<BlockStmt>(std::move(stmts), tok.line, tok.col);
	} else {
		consume(
			TokenType::SEMI_COLON,
			"Expected function body '{', '=>' or ';' for function prototype"
		);
	}

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
	consume(TokenType::OPEN_PAREN, "Expected '(' for struct field declarations");
	if (!check(TokenType::CLOSE_PAREN)) {
		do {
			const Token f_name = consume(TokenType::IDENTIFIER, "Expected field name");
			consume(TokenType::COLON, "Expected ':' after field name");
			auto f_type = parse_type();
			fields.push_back(StructField{f_name.text, std::move(f_type)});
		} while (match(TokenType::COMMA));
	}
	consume(TokenType::CLOSE_PAREN, "Expected ')' to close struct field declarations");

	std::vector<std::unique_ptr<FnDecl>> methods;
	if (match(TokenType::OPEN_BRACE)) {
		while (!check(TokenType::CLOSE_BRACE) && !is_end()) {
			const bool method_pub = match(TokenType::KW_PUB);
			if (check(TokenType::KW_FN)) {
				auto fn = parse_fn_decl();
				if (fn) {
					fn->is_pub = method_pub;
					methods.push_back(std::move(fn));
				}
			} else {
				advance();
			}
		}
		consume(TokenType::CLOSE_BRACE, "Expected '}' to end struct definition");
	} else {
		match(TokenType::SEMI_COLON);
	}

	auto st = std::make_unique<StructDecl>(name.text, tok.line, tok.col);
	st->fields = std::move(fields);
	st->methods = std::move(methods);
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

	consume(TokenType::EQUAL, "Expected '=' in constant declaration");
	auto val = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after constant declaration");
	return std::make_unique<ConstDecl>(name.text, std::move(type), std::move(val), tok.line, tok.col);
}

std::unique_ptr<ExternBlock> Parser::parse_extern_block() {
	const auto tok = consume(TokenType::KW_EXTERN, "Expected 'extern'");
	const auto abi = consume(TokenType::STRING, "Expected ABI string (e.g., \"libc\", \"C\") after 'extern'");

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin extern block");
	std::vector<std::unique_ptr<FnDecl>> declarations;

	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		if (check(TokenType::KW_FN))
			declarations.push_back(parse_fn_decl());
		else advance();
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end extern block");
	auto ext = std::make_unique<ExternBlock>(abi.text, tok.line, tok.col);
	ext->declarations = std::move(declarations);
	return ext;
}

std::unique_ptr<ModuleDecl> Parser::parse_module_decl() {
	const auto tok = consume(TokenType::KW_MODULE, "Expected 'module'");
	std::vector<std::string_view> path;

	const auto first_seg = consume(TokenType::IDENTIFIER, "Expected module path segment");
	path.push_back(first_seg.text);

	while (match(TokenType::DOT)) {
		const auto seg = consume(TokenType::IDENTIFIER, "Expected module path segment after '.'");
		path.push_back(seg.text);
	}

	consume(TokenType::SEMI_COLON, "Expected ';' after module declaration");
	return std::make_unique<ModuleDecl>(std::move(path), tok.line, tok.col);
}

std::unique_ptr<UseDecl> Parser::parse_use_decl() {
	const auto tok = consume(TokenType::KW_USE, "Expected 'use'");
	std::vector<std::string_view> segments;
	bool is_wildcard = false;

	const auto first_seg = consume(TokenType::IDENTIFIER, "Expected module path or symbol name after 'use'");
	segments.push_back(first_seg.text);

	while (match(TokenType::DOT)) {
		if (match(TokenType::STAR)) {
			is_wildcard = true;
			break;
		}
		const auto seg = consume(TokenType::IDENTIFIER, "Expected module path segment, symbol name or '*' after '.'");
		segments.push_back(seg.text);
	}

	std::string_view alias;
	if (!is_wildcard && match(TokenType::KW_AS)) {
		const auto alias_tok = consume(TokenType::IDENTIFIER, "Expected alias identifier after 'as'");
		alias = alias_tok.text;
	}

	consume(TokenType::SEMI_COLON, "Expected ';' after use declaration");

	std::vector<std::string_view> path;
	std::string_view symbol_name;

	if (is_wildcard) {
		path = std::move(segments);
	} else if (segments.size() == 1) {
		symbol_name = segments[0];
	} else {
		symbol_name = segments.back();
		segments.pop_back();
		path = std::move(segments);
	}

	return std::make_unique<UseDecl>(std::move(path), symbol_name, alias, is_wildcard, tok.line, tok.col);
}

std::unique_ptr<Decl> Parser::parse_declaration() {
	bool is_pub = false;
	if (match(TokenType::KW_PUB)) {
		is_pub = true;
	}

	if (check(TokenType::KW_MODULE)) {
		if (is_pub) {
			error(previous(), "'pub' cannot be applied to 'module' declaration");
		}
		return parse_module_decl();
	}

	if (check(TokenType::KW_USE)) {
		if (is_pub) {
			error(previous(), "'pub' cannot be applied to 'use' declaration");
		}
		return parse_use_decl();
	}

	std::unique_ptr<Decl> decl = nullptr;
	if (check(TokenType::KW_FN)) decl = parse_fn_decl();
	else if (check(TokenType::KW_STRUCT)) decl = parse_struct_decl();
	else if (check(TokenType::KW_ENUM)) decl = parse_enum_decl();
	else if (check(TokenType::KW_CONST)) decl = parse_const_decl();
	else if (check(TokenType::KW_EXTERN)) decl = parse_extern_block();
	else {
		error(peek(), "Expected top-level declaration ('fn', 'struct', 'enum', 'const', 'extern', 'module', 'use')");
		advance();
		return nullptr;
	}

	if (decl) {
		decl->is_pub = is_pub;
	}
	return decl;
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
