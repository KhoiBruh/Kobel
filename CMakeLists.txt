cmake_minimum_required(VERSION 4.2)

project(Kobel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "C:/LLVM/lib/cmake/llvm")

if(NOT TARGET zstd::libzstd_static)
	add_library(zstd::libzstd_static INTERFACE IMPORTED)
endif()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

llvm_map_components_to_libnames(llvm_libs
	Support
	Core
	IRReader
)

add_executable(
	Kobel src/main.cpp

	# Lexer
	src/lexer/Lexer.cpp
	src/lexer/Token.cpp

)

target_link_libraries(Kobel PRIVATE ${llvm_libs})
