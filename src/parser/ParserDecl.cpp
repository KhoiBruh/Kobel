module;

#include <memory>
#include <string>
#include <string_view>
#include <vector>

module parser;

import ast;
import token;

std::vector<GenericParam> Parser::parse_generic_params() {
	std::vector<GenericParam> type_params;
	if (match(TokenType::LESS)) {
		do {
			const Token p_name = consume(TokenType::IDENTIFIER, "Expected type parameter name");
			std::vector<std::string_view> bounds;
			if (match(TokenType::COLON)) {
				do {
					const Token b_name = consume(TokenType::IDENTIFIER, "Expected trait bound name");
					bounds.push_back(b_name.text);
				} while (match(TokenType::PLUS));
			}
			type_params.push_back(GenericParam{p_name.text, arena.alloc_span(bounds)});
		} while (match(TokenType::COMMA));
		consume(TokenType::GREATER, "Expected '>' after type parameters");
	}
	return type_params;
}

void Parser::append_unique_generic_params(std::vector<GenericParam> &type_params) {
	auto extra = parse_generic_params();
	for (auto &&tp : extra) {
		bool exists = false;
		for (const auto &existing : type_params) {
			if (existing.name == tp.name) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			type_params.push_back(std::move(tp));
		}
	}
}

FnDecl *Parser::parse_fn_decl() {
	const auto tok = consume(TokenType::KW_FN, "Expected 'fn'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected function name after 'fn'");

	auto type_params = parse_generic_params();

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
			TypeNode *p_type = nullptr;
			if (match(TokenType::COLON)) {
				p_type = parse_type();
			} else if (p_name.text != "self") {
				error(p_name, "Expected ':' and type after parameter name");
			}

			params.push_back(Param{p_name.text, p_type, is_mut, has_val});
		} while (match(TokenType::COMMA));
	}
	consume(TokenType::CLOSE_PAREN, "Expected ')' to close parameter list");

	TypeNode *ret_type = nullptr;
	if (match(TokenType::COLON)) ret_type = parse_type();

	BlockStmt *body = nullptr;
	if (check(TokenType::OPEN_BRACE)) {
		body = parse_block_stmt();
	} else if (match(TokenType::FAT_ARROW)) {
		auto expr = parse_expression();
		consume(TokenType::SEMI_COLON, "Expected ';' after expression body");
		auto ret_stmt = arena.alloc<ReturnStmt>(expr, tok.line, tok.col);
		body = arena.alloc<BlockStmt>(arena.alloc_span<Stmt *>({ret_stmt}), tok.line, tok.col);
	} else {
		consume(
			TokenType::SEMI_COLON,
			"Expected function body '{', '=>' or ';' for function prototype"
		);
	}

	auto fn = arena.alloc<FnDecl>(name.text, tok.line, tok.col);
	fn->type_params = arena.alloc_span(type_params);
	fn->params = arena.alloc_span(params);
	fn->return_type = ret_type;
	fn->body = body;
	return fn;
}

StructDecl *Parser::parse_struct_decl() {
	const auto tok = consume(TokenType::KW_STRUCT, "Expected 'struct'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected struct name");

	auto type_params = parse_generic_params();

	std::vector<StructField> fields;
	std::vector<std::string_view> traits;
	std::vector<FnDecl *> methods;

	if (match(TokenType::OPEN_PAREN)) {
		// Old syntax: struct Point(x: i32, y: i32)
		if (!check(TokenType::CLOSE_PAREN)) {
			do {
				bool is_field_pub = match(TokenType::KW_PUB);
				const Token f_name = consume(TokenType::IDENTIFIER, "Expected field name");
				consume(TokenType::COLON, "Expected ':' after field name");
				auto f_type = parse_type();
				fields.push_back(StructField{f_name.text, f_type, true});
			} while (match(TokenType::COMMA));
		}
		consume(TokenType::CLOSE_PAREN, "Expected ')' to close struct field declarations");

		if (match(TokenType::COLON)) {
			do {
				const Token t_name = consume(TokenType::IDENTIFIER, "Expected trait name");
				traits.push_back(t_name.text);
				if (check(TokenType::PLUS)) {
					advance();
				} else if (check(TokenType::COMMA)) {
					advance();
				} else {
					break;
				}
			} while (!check(TokenType::OPEN_BRACE) && !check(TokenType::SEMI_COLON) && !is_end());
		}

		if (match(TokenType::OPEN_BRACE)) {
			while (!check(TokenType::CLOSE_BRACE) && !is_end()) {
				bool method_pub = false;
				bool is_override = false;
				while (check(TokenType::KW_PUB) || check(TokenType::KW_OVERRIDE)) {
					if (match(TokenType::KW_PUB)) method_pub = true;
					else if (match(TokenType::KW_OVERRIDE)) is_override = true;
				}
				if (check(TokenType::KW_FN)) {
					auto fn = parse_fn_decl();
					if (fn) {
						fn->is_pub = method_pub;
						fn->is_override = is_override;
						methods.push_back(fn);
					}
				} else {
					advance();
				}
			}
			consume(TokenType::CLOSE_BRACE, "Expected '}' to end struct definition");
		} else {
			match(TokenType::SEMI_COLON);
		}
	} else {
		// New syntax: struct List<T> { pub data: &T, len: usz, cap: usz }
		if (match(TokenType::COLON)) {
			do {
				const Token t_name = consume(TokenType::IDENTIFIER, "Expected trait name");
				traits.push_back(t_name.text);
				if (check(TokenType::PLUS)) {
					advance();
				} else if (check(TokenType::COMMA)) {
					advance();
				} else {
					break;
				}
			} while (!check(TokenType::OPEN_BRACE) && !check(TokenType::SEMI_COLON) && !is_end());
		}

		if (match(TokenType::OPEN_BRACE)) {
			while (!check(TokenType::CLOSE_BRACE) && !is_end()) {
				bool is_pub = false;
				bool is_override = false;
				while (check(TokenType::KW_PUB) || check(TokenType::KW_OVERRIDE)) {
					if (match(TokenType::KW_PUB)) is_pub = true;
					else if (match(TokenType::KW_OVERRIDE)) is_override = true;
				}

				if (check(TokenType::KW_FN)) {
					auto fn = parse_fn_decl();
					if (fn) {
						fn->is_pub = is_pub;
						fn->is_override = is_override;
						methods.push_back(fn);
					}
				} else if (check(TokenType::IDENTIFIER)) {
					const Token f_name = advance();
					consume(TokenType::COLON, "Expected ':' after field name");
					auto f_type = parse_type();
					fields.push_back(StructField{f_name.text, f_type, is_pub});
					if (check(TokenType::COMMA) || check(TokenType::SEMI_COLON)) {
						advance();
					}
				} else {
					error(peek(), "Expected field or method declaration in struct");
					advance();
				}
			}
			consume(TokenType::CLOSE_BRACE, "Expected '}' to end struct definition");
		} else {
			match(TokenType::SEMI_COLON);
		}
	}

	auto st = arena.alloc<StructDecl>(name.text, tok.line, tok.col);
	st->type_params = arena.alloc_span(type_params);
	st->fields = arena.alloc_span(fields);
	st->traits = arena.alloc_span(traits);
	st->methods = arena.alloc_span(methods);
	return st;
}

ImplDecl *Parser::parse_impl_decl() {
	const auto tok = consume(TokenType::KW_IMPL, "Expected 'impl'");

	auto type_params = parse_generic_params();

	const Token first_tok = consume(TokenType::IDENTIFIER, "Expected struct or trait name after 'impl'");

	append_unique_generic_params(type_params);

	std::string_view struct_name = first_tok.text;
	std::string_view trait_name = "";

	if (match(TokenType::KW_FOR)) {
		trait_name = first_tok.text;
		const Token st_tok = consume(TokenType::IDENTIFIER, "Expected struct name after 'for'");
		struct_name = st_tok.text;
		append_unique_generic_params(type_params);
	} else if (match(TokenType::COLON)) {
		struct_name = first_tok.text;
		const Token tr_tok = consume(TokenType::IDENTIFIER, "Expected trait name after ':'");
		trait_name = tr_tok.text;
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin 'impl' body");

	std::vector<FnDecl *> methods;
	while (!check(TokenType::CLOSE_BRACE) && !is_end()) {
		bool method_pub = false;
		bool is_override = false;
		while (check(TokenType::KW_PUB) || check(TokenType::KW_OVERRIDE)) {
			if (match(TokenType::KW_PUB)) method_pub = true;
			else if (match(TokenType::KW_OVERRIDE)) is_override = true;
		}
		if (check(TokenType::KW_FN)) {
			auto fn = parse_fn_decl();
			if (fn) {
				fn->is_pub = method_pub;
				fn->is_override = is_override;
				methods.push_back(fn);
			}
		} else {
			error(peek(), "Expected method declaration in 'impl' block");
			advance();
		}
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end 'impl' body");

	auto impl_decl = arena.alloc<ImplDecl>(struct_name, tok.line, tok.col);
	impl_decl->type_params = arena.alloc_span(type_params);
	impl_decl->trait_name = trait_name;
	impl_decl->methods = arena.alloc_span(methods);
	return impl_decl;
}

TraitDecl *Parser::parse_trait_decl() {
	const auto tok = consume(TokenType::KW_TRAIT, "Expected 'trait'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected trait name after 'trait'");

	auto type_params = parse_generic_params();

	std::vector<std::string_view> bases;
	if (match(TokenType::COLON)) {
		do {
			const Token b_name = consume(TokenType::IDENTIFIER, "Expected base trait name");
			bases.push_back(b_name.text);
			if (check(TokenType::PLUS)) {
				advance();
			} else if (check(TokenType::COMMA)) {
				advance();
			} else {
				break;
			}
		} while (!check(TokenType::OPEN_BRACE) && !is_end());
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin trait body");

	std::vector<FnDecl *> methods;
	while (!check(TokenType::CLOSE_BRACE) && !is_end()) {
		const bool method_pub = match(TokenType::KW_PUB);
		if (check(TokenType::KW_FN)) {
			auto fn = parse_fn_decl();
			if (fn) {
				fn->is_pub = method_pub;
				methods.push_back(fn);
			}
		} else {
			advance();
		}
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end trait body");

	auto tr = arena.alloc<TraitDecl>(name.text, tok.line, tok.col);
	tr->type_params = arena.alloc_span(type_params);
	tr->bases = arena.alloc_span(bases);
	tr->methods = arena.alloc_span(methods);
	return tr;
}

EnumDecl *Parser::parse_enum_decl() {
	const auto tok = consume(TokenType::KW_ENUM, "Expected 'enum'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected enum name");

	TypeNode *underlying = nullptr;
	if (match(TokenType::COLON)) {
		underlying = parse_type();
	}

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin enum body");

	std::vector<EnumMember> members;
	if (!check(TokenType::CLOSE_BRACE)) {
		do {
			if (check(TokenType::CLOSE_BRACE)) break;
			const Token m_name = consume(TokenType::IDENTIFIER, "Expected enum member name");
			Expr *val = nullptr;
			if (match(TokenType::EQUAL)) {
				val = parse_expression();
			}
			members.push_back(EnumMember{m_name.text, val, m_name.line, m_name.col});
		} while (match(TokenType::COMMA));
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end enum body");

	auto enum_decl = arena.alloc<EnumDecl>(name.text, tok.line, tok.col);
	enum_decl->underlying_type = underlying;
	enum_decl->members = arena.alloc_span(members);
	return enum_decl;
}

ConstDecl *Parser::parse_const_decl() {
	const auto tok = consume(TokenType::KW_CONST, "Expected 'const'");
	const auto name = consume(TokenType::IDENTIFIER, "Expected constant name");

	consume(TokenType::COLON, "Expected ':' after constant name");
	auto type = parse_type();

	consume(TokenType::EQUAL, "Expected '=' in constant declaration");
	auto val = parse_expression();

	consume(TokenType::SEMI_COLON, "Expected ';' after constant declaration");
	return arena.alloc<ConstDecl>(name.text, type, val, tok.line, tok.col);
}

ExternBlock *Parser::parse_extern_block() {
	const auto tok = consume(TokenType::KW_EXTERN, "Expected 'extern'");
	const auto abi = consume(TokenType::STRING, "Expected ABI string (e.g., \"libc\", \"C\") after 'extern'");

	consume(TokenType::OPEN_BRACE, "Expected '{' to begin extern block");
	std::vector<FnDecl *> declarations;

	while (!is_end() && !check(TokenType::CLOSE_BRACE)) {
		if (check(TokenType::KW_FN))
			declarations.push_back(parse_fn_decl());
		else advance();
	}

	consume(TokenType::CLOSE_BRACE, "Expected '}' to end extern block");
	auto ext = arena.alloc<ExternBlock>(abi.text, tok.line, tok.col);
	ext->declarations = arena.alloc_span<FnDecl *>(declarations);
	return ext;
}

ModuleDecl *Parser::parse_module_decl() {
	const auto tok = consume(TokenType::KW_MOD, "Expected 'mod'");
	std::vector<std::string_view> path;

	const auto first_seg = consume(TokenType::IDENTIFIER, "Expected module path segment");
	path.push_back(first_seg.text);

	while (match(TokenType::DOT)) {
		const auto seg = consume(TokenType::IDENTIFIER, "Expected module path segment after '.'");
		path.push_back(seg.text);
	}

	consume(TokenType::SEMI_COLON, "Expected ';' after module declaration");
	std::string full_path;
	for (size_t i = 0; i < path.size(); ++i) {
		if (i > 0) full_path += ".";
		full_path += path[i];
	}
	return arena.alloc<ModuleDecl>(arena.alloc_span(path), arena.alloc_string(full_path), tok.line, tok.col);
}

UseDecl *Parser::parse_use_decl() {
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
		path = segments;
	} else if (segments.size() == 1) {
		symbol_name = segments[0];
	} else {
		symbol_name = segments.back();
		segments.pop_back();
		path = segments;
	}

	std::string full_path;
	for (size_t i = 0; i < path.size(); ++i) {
		if (i > 0) full_path += ".";
		full_path += path[i];
	}
	return arena.alloc<UseDecl>(
		arena.alloc_span(path), arena.alloc_string(full_path), symbol_name, alias, is_wildcard, tok.line, tok.col
	);
}

Decl *Parser::parse_declaration() {
	bool is_pub = false;
	if (match(TokenType::KW_PUB)) {
		is_pub = true;
	}

	if (check(TokenType::KW_MOD)) {
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

	Decl *decl = nullptr;
	if (check(TokenType::KW_FN)) decl = parse_fn_decl();
	else if (check(TokenType::KW_STRUCT)) decl = parse_struct_decl();
	else if (check(TokenType::KW_TRAIT)) decl = parse_trait_decl();
	else if (check(TokenType::KW_IMPL)) decl = parse_impl_decl();
	else if (check(TokenType::KW_ENUM)) decl = parse_enum_decl();
	else if (check(TokenType::KW_CONST)) decl = parse_const_decl();
	else if (check(TokenType::KW_EXTERN)) decl = parse_extern_block();
	else {
		error(peek(), "Expected top-level declaration ('fn', 'struct', 'trait', 'impl', 'enum', 'const', 'extern', 'mod', 'use')");
		advance();
		return nullptr;
	}

	if (decl) {
		decl->is_pub = is_pub;
	}
	return decl;
}

Program *Parser::parse_program() {
	auto program = arena.alloc<Program>();
	std::vector<Decl *> decls;
	while (!is_end()) {
		if (auto decl = parse_declaration())
			decls.push_back(decl);
		else synchronize();
	}
	program->declarations = arena.alloc_span<Decl *>(decls);
	return program;
}
