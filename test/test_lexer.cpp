#include <iostream>
#include <string_view>
#include <vector>

import token;
import lexer;

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			std::cerr << "[FAILED] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			return false; \
		} \
	} while (0)

bool test_keywords() {
	std::string_view code = "extern struct fn return const null true false val var as while break continue if else";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	std::vector<TokenType> expected = {
		TokenType::KW_EXTERN, TokenType::KW_STRUCT, TokenType::KW_FN,
		TokenType::KW_RETURN, TokenType::KW_CONST, TokenType::KW_NULL,
		TokenType::KW_TRUE, TokenType::KW_FALSE, TokenType::KW_VAL,
		TokenType::KW_VAR, TokenType::KW_AS, TokenType::KW_WHILE,
		TokenType::KW_BREAK, TokenType::KW_CONTINUE, TokenType::KW_IF,
		TokenType::KW_ELSE, TokenType::END_OF_FILE
	};

	ASSERT(tokens.size() == expected.size(), "Token count mismatch in keywords");
	for (size_t i = 0; i < expected.size(); ++i) {
		ASSERT(tokens[i].type == expected[i], "Keyword type mismatch");
	}
	return true;
}

bool test_operators() {
	std::string_view code = "= == + - * / % < <= > >= ! != && || : ; , . [ ] ( ) { }";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	std::vector expected = {
		TokenType::EQUAL, TokenType::EQUAL_EQUAL,
		TokenType::PLUS, TokenType::MINUS,
		TokenType::STAR, TokenType::SLASH, TokenType::PERCENT,
		TokenType::LESS, TokenType::LESS_EQUAL,
		TokenType::GREATER, TokenType::GREATER_EQUAL,
		TokenType::BANG, TokenType::BANG_EQUAL,
		TokenType::AND_AND, TokenType::OR_OR,
		TokenType::COLON, TokenType::SEMI_COLON, TokenType::COMMA, TokenType::DOT,
		TokenType::OPEN_BRACKET, TokenType::CLOSE_BRACKET,
		TokenType::OPEN_PAREN, TokenType::CLOSE_PAREN,
		TokenType::OPEN_BRACE, TokenType::CLOSE_BRACE,
		TokenType::END_OF_FILE
	};

	ASSERT(tokens.size() == expected.size(), "Token count mismatch in operators");
	for (size_t i = 0; i < expected.size(); ++i) {
		ASSERT(tokens[i].type == expected[i], "Operator type mismatch");
	}
	return true;
}

bool test_literals() {
	std::string_view code = "123 45.67 \"hello world\" 'a' '\\n'";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	ASSERT(tokens.size() == 6, "Literal token count mismatch");
	ASSERT(tokens[0].type == TokenType::NUMBER && tokens[0].text == "123", "Integer mismatch");
	ASSERT(tokens[1].type == TokenType::NUMBER && tokens[1].text == "45.67", "Float mismatch");
	ASSERT(tokens[2].type == TokenType::STRING && tokens[2].text == "\"hello world\"", "String mismatch");
	ASSERT(tokens[3].type == TokenType::CHAR && tokens[3].text == "'a'", "Char mismatch");
	ASSERT(tokens[4].type == TokenType::CHAR && tokens[4].text == "'\\n'", "Escape char mismatch");
	ASSERT(tokens[5].type == TokenType::END_OF_FILE, "EOF mismatch");
	return true;
}

bool test_comments_and_whitespace() {
	std::string_view code = "val a = 1; // single line comment\nval b = 2; /* multi\nline\ncomment */ val c = 3;";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	std::vector<std::string_view> expected_texts = {
		"val", "a", "=", "1", ";",
		"val", "b", "=", "2", ";",
		"val", "c", "=", "3", ";",
		""
	};

	ASSERT(tokens.size() == expected_texts.size(), "Comment filtering count mismatch");
	for (size_t i = 0; i < expected_texts.size(); ++i) {
		ASSERT(tokens[i].text == expected_texts[i], "Comment filtering token text mismatch");
	}
	return true;
}

bool test_line_col_tracking() {
	std::string_view code = "val x = 1;\nval y = 2;";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	ASSERT(tokens[0].text == "val" && tokens[0].line == 1 && tokens[0].col == 1, "Line 1 col tracking mismatch");
	ASSERT(tokens[5].text == "val" && tokens[5].line == 2 && tokens[5].col == 1, "Line 2 col tracking mismatch");
	return true;
}

bool test_numeric_prefixes_and_suffixes() {
	std::string_view code = "0xFF 0b1010 0o77 1_000_000 100L 50U 10_000_US 3.14F 2.5D";
	Lexer lex{code};
	auto tokens = lex.tokenize();

	ASSERT(tokens.size() == 10, "Prefixes token count mismatch");
	ASSERT(tokens[0].type == TokenType::NUMBER && tokens[0].text == "0xFF", "Hex mismatch");
	ASSERT(tokens[1].type == TokenType::NUMBER && tokens[1].text == "0b1010", "Binary mismatch");
	ASSERT(tokens[2].type == TokenType::NUMBER && tokens[2].text == "0o77", "Octal mismatch");
	ASSERT(tokens[3].type == TokenType::NUMBER && tokens[3].text == "1_000_000", "Underscore mismatch");
	ASSERT(tokens[4].type == TokenType::NUMBER && tokens[4].text == "100L", "Suffix L mismatch");
	ASSERT(tokens[5].type == TokenType::NUMBER && tokens[5].text == "50U", "Suffix U mismatch");
	ASSERT(tokens[6].type == TokenType::NUMBER && tokens[6].text == "10_000_US", "Suffix US mismatch");
	ASSERT(tokens[7].type == TokenType::NUMBER && tokens[7].text == "3.14F", "Suffix F mismatch");
	ASSERT(tokens[8].type == TokenType::NUMBER && tokens[8].text == "2.5D", "Suffix D mismatch");
	ASSERT(tokens[9].type == TokenType::END_OF_FILE, "EOF mismatch");
	return true;
}

int main() {
	std::cout << "[RUNNING] Lexer tests..." << std::endl;
	if (!test_keywords()) return 1;
	std::cout << "  [PASS] test_keywords" << std::endl;

	if (!test_operators()) return 1;
	std::cout << "  [PASS] test_operators" << std::endl;

	if (!test_literals()) return 1;
	std::cout << "  [PASS] test_literals" << std::endl;

	if (!test_numeric_prefixes_and_suffixes()) return 1;
	std::cout << "  [PASS] test_numeric_prefixes_and_suffixes" << std::endl;

	if (!test_comments_and_whitespace()) return 1;
	std::cout << "  [PASS] test_comments_and_whitespace" << std::endl;

	if (!test_line_col_tracking()) return 1;
	std::cout << "  [PASS] test_line_col_tracking" << std::endl;

	std::cout << "[ALL PASSED] Lexer tests passed successfully!" << std::endl;
	return 0;
}

