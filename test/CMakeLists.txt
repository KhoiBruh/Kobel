add_executable(test_lexer test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE kobel_core)
add_test(NAME LexerTests COMMAND test_lexer)

add_executable(test_ast test_ast.cpp)
target_link_libraries(test_ast PRIVATE kobel_core)
add_test(NAME AstTests COMMAND test_ast)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE kobel_core)
add_test(NAME ParserTests COMMAND test_parser)

add_executable(test_semantic test_semantic.cpp)
target_link_libraries(test_semantic PRIVATE kobel_core)
add_test(NAME SemanticTests COMMAND test_semantic)
