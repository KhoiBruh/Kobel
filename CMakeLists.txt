cmake_minimum_required(VERSION 4.2)
project(Kobel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (DEFINED ENV{LLVM_DIR})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{LLVM_DIR}")
elseif (DEFINED ENV{LLVM_PATH})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{LLVM_PATH}/lib/cmake/llvm")
elseif (EXISTS "~/LLVM/lib/cmake/llvm")
	list(APPEND CMAKE_PREFIX_PATH "~/LLVM/lib/cmake/llvm")
endif ()

if (NOT TARGET zstd::libzstd_static)
	add_library(zstd::libzstd_static INTERFACE IMPORTED)
	if (EXISTS "C:/Users/Admin/.vcpkg-clion/vcpkg/installed/x64-windows/debug/lib/zstd.lib")
		set_target_properties(zstd::libzstd_static PROPERTIES
				INTERFACE_LINK_LIBRARIES "$<IF:$<CONFIG:Debug>,C:/Users/Admin/.vcpkg-clion/vcpkg/installed/x64-windows/debug/lib/zstd.lib,C:/Users/Admin/.vcpkg-clion/vcpkg/installed/x64-windows/lib/zstd.lib>"
		)
	elseif (EXISTS "$ENV{USERPROFILE}/.vcpkg-clion/vcpkg/installed/x64-windows/debug/lib/zstd.lib")
		set_target_properties(zstd::libzstd_static PROPERTIES
				INTERFACE_LINK_LIBRARIES "$<IF:$<CONFIG:Debug>,$ENV{USERPROFILE}/.vcpkg-clion/vcpkg/installed/x64-windows/debug/lib/zstd.lib,$ENV{USERPROFILE}/.vcpkg-clion/vcpkg/installed/x64-windows/lib/zstd.lib>"
		)
	elseif (EXISTS "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/debug/lib/zstd.lib")
		set_target_properties(zstd::libzstd_static PROPERTIES
				INTERFACE_LINK_LIBRARIES "$<IF:$<CONFIG:Debug>,$ENV{USERPROFILE}/vcpkg/installed/x64-windows/debug/lib/zstd.lib,$ENV{USERPROFILE}/vcpkg/installed/x64-windows/lib/zstd.lib>"
		)
	endif ()
endif ()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

if (MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
else ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif ()

llvm_map_components_to_libnames(llvm_libs
		Support
		Core
		IRReader
		TargetParser
		Target
		MC
		X86
		X86CodeGen
		X86Desc
		X86Info
		X86AsmParser
		AsmPrinter
		Passes
)

add_library(kobel_core STATIC)

target_sources(kobel_core
		PUBLIC
		FILE_SET CXX_MODULES FILES
		src/kobel/kobel.cppm

		src/kobel/ast/mod.cppm
		src/kobel/lexer/mod.cppm
		src/kobel/logger/mod.cppm
		src/kobel/parser/mod.cppm

		src/kobel/ast/ASTKind.cppm
		src/kobel/ast/ASTNode.cppm
		src/kobel/ast/Arena.cppm
		src/kobel/ast/Type.cppm
		src/kobel/ast/Expr.cppm
		src/kobel/ast/Stmt.cppm
		src/kobel/ast/Decl.cppm

		src/kobel/lexer/Lexer.cppm
		src/kobel/lexer/Token.cppm
		src/kobel/lexer/TokenType.cppm

		src/kobel/logger/Engine.cppm
		src/kobel/logger/Diagnostic.cppm
		src/kobel/logger/DiagnosticType.cppm

		src/kobel/parser/Precedence.cppm
		src/kobel/parser/Parser.cppm
)

target_sources(kobel_core
		PRIVATE

		src/kobel/lexer/Lexer.cpp
		src/kobel/lexer/Tokenizer.cpp

		src/kobel/logger/Diagnostic.cpp
		src/kobel/logger/Engine.cpp

		src/kobel/parser/Parser.cpp
		src/kobel/parser/ParserType.cpp
		src/kobel/parser/ParserExpr.cpp
		src/kobel/parser/ParserStmt.cpp
		src/kobel/parser/ParserDecl.cpp
)

target_link_libraries(kobel_core PRIVATE ${llvm_libs})

add_executable(Kobel src/Main.cpp)
target_link_libraries(Kobel PRIVATE kobel_core)

enable_testing()
add_subdirectory(test)
