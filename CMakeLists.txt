cmake_minimum_required(VERSION 4.2)
project(Kobel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (DEFINED ENV{LLVM_DIR})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{LLVM_DIR}")
elseif (DEFINED ENV{LLVM_PATH})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{LLVM_PATH}/lib/cmake/llvm")
elseif (EXISTS "C:/LLVM/lib/cmake/llvm")
	list(APPEND CMAKE_PREFIX_PATH "C:/LLVM/lib/cmake/llvm")
endif ()

if (NOT TARGET zstd::libzstd_static)
	add_library(zstd::libzstd_static INTERFACE IMPORTED)
	set_target_properties(zstd::libzstd_static PROPERTIES
		INTERFACE_LINK_LIBRARIES "$<IF:$<CONFIG:Debug>,C:/Users/Admin/.vcpkg-clion/vcpkg/installed/x64-windows/debug/lib/zstd.lib,C:/Users/Admin/.vcpkg-clion/vcpkg/installed/x64-windows/lib/zstd.lib>"
	)
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
)

add_library(kobel_core STATIC)

target_sources(kobel_core
	PUBLIC
	FILE_SET CXX_MODULES FILES

	src/ast/Base.cpp
	src/ast/Type.cpp
	src/ast/Expr.cpp
	src/ast/Stmt.cpp
	src/ast/Decl.cpp
	src/ast/AST.cpp

	src/lexer/Lexer.cpp
	src/lexer/Token.cpp
	src/lexer/TokenMap.cpp

	src/logger/Logger.cpp
	src/parser/Parser.cpp

	src/semantic/Semantic.cpp
	src/semantic/Symbol.cpp
	src/semantic/Analyzer.cpp

	src/codegen/CodeGen.cpp

	src/driver/Driver.cpp
)

target_sources(kobel_core
	PRIVATE
	src/parser/ParserType.cpp
	src/parser/ParserExpr.cpp
	src/parser/ParserStmt.cpp
	src/parser/ParserDecl.cpp

	src/semantic/AnalyzerModule.cpp
	src/semantic/AnalyzerType.cpp
	src/semantic/AnalyzerDecl.cpp
	src/semantic/AnalyzerStmt.cpp
	src/semantic/AnalyzerExpr.cpp

	src/codegen/CodeGenExpr.cpp
	src/codegen/CodeGenStmt.cpp
	src/codegen/CodeGenDecl.cpp
	src/codegen/CodeGenNative.cpp
)

target_link_libraries(kobel_core PRIVATE ${llvm_libs})

add_executable(Kobel src/main.cpp)
target_link_libraries(Kobel PRIVATE kobel_core)

enable_testing()
add_subdirectory(test)
