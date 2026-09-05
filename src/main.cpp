#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import lexer;
import parser;
import semantic;
import semantic.analyzer;
import codegen;
import logger;
import ast;

namespace {
	enum class OutputMode {
		EXECUTABLE,
		OBJECT,
		ASSEMBLY,
		IR
	};

	void print_version() {
		std::cout << "Kobel Compiler v0.1.0 (x86_64 Windows)\n";
	}

	void print_help(const char *prog_name) {
		std::cout << "Usage: " << prog_name << " [options] <source files...>\n\n"
			<< "Options:\n"
			<< "  -o <file>          Specify output file name\n"
			<< "  -c, --emit-obj     Emit native object file (.obj) and stop\n"
			<< "  -S, --emit-asm     Emit x86_64 assembly (.s) and stop\n"
			<< "  --emit-ir          Emit LLVM IR (.ll) and stop\n"
			<< "  --target <triple>  Specify target triple (default: x86_64-pc-windows-msvc)\n"
			<< "  -h, --help         Display this help message\n"
			<< "  -v, --version      Display compiler version\n";
	}

	std::string read_file_content(const std::string &path) {
		const std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file.is_open()) return "";
		std::ostringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		print_help(argv[0]);
		return 1;
	}

	std::vector<std::string> input_files;
	std::string output_file;
	std::string target_triple = "x86_64-pc-windows-msvc";
	auto mode = OutputMode::EXECUTABLE;
	bool explicit_mode = false;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];

		if (arg == "-h" || arg == "--help") {
			print_help(argv[0]);
			return 0;
		}
		if (arg == "-v" || arg == "--version") {
			print_version();
			return 0;
		}
		if (arg == "-o") {
			if (i + 1 < argc) {
				output_file = argv[++i];
			} else {
				std::cerr << "Error: Missing argument after '-o'\n";
				return 1;
			}
		} else if (arg == "-c" || arg == "--emit-obj") {
			mode = OutputMode::OBJECT;
			explicit_mode = true;
		} else if (arg == "-S" || arg == "--emit-asm") {
			mode = OutputMode::ASSEMBLY;
			explicit_mode = true;
		} else if (arg == "--emit-ir") {
			mode = OutputMode::IR;
			explicit_mode = true;
		} else if (arg == "--target") {
			if (i + 1 < argc) {
				target_triple = argv[++i];
			} else {
				std::cerr << "Error: Missing argument after '--target'\n";
				return 1;
			}
		} else if (arg.starts_with("-")) {
			std::cerr << "Error: Unknown option '" << arg << "'\n";
			return 1;
		} else {
			input_files.push_back(std::string(arg));
		}
	}

	if (input_files.empty()) {
		std::cerr << "Error: No input files provided.\n";
		return 1;
	}

	// Xác định tên file đầu ra mặc định nếu chưa chỉ định
	auto base_name = std::filesystem::path(input_files[0]).stem().string();
	if (output_file.empty()) {
		switch (mode) {
			case OutputMode::EXECUTABLE:
				output_file = base_name + ".exe";
				break;
			case OutputMode::OBJECT:
				output_file = base_name + ".obj";
				break;
			case OutputMode::ASSEMBLY:
				output_file = base_name + ".s";
				break;
			case OutputMode::IR:
				output_file = base_name + ".ll";
				break;
		}
	} else if (!explicit_mode) {
		// Tự động suy luận mode dựa trên đuôi file của -o nếu người dùng không dùng cờ tường minh
		if (output_file.ends_with(".ll")) mode = OutputMode::IR;
		else if (output_file.ends_with(".s") || output_file.ends_with(".asm")) mode = OutputMode::ASSEMBLY;
		else if (output_file.ends_with(".obj") || output_file.ends_with(".o")) mode = OutputMode::OBJECT;
	}

	// 1. Nạp an toàn nội dung toàn bộ các file nguồn vào bộ nhớ
	// Sử dụng std::unique_ptr<std::string> để bảo đảm pointer stability cho std::string_view
	std::vector<std::unique_ptr<std::string> > source_buffers;
	source_buffers.reserve(input_files.size());

	std::vector<std::unique_ptr<Program> > parsed_programs;
	parsed_programs.reserve(input_files.size());

	bool has_syntax_errors = false;

	for (const auto &filepath: input_files) {
		if (!std::filesystem::exists(filepath)) {
			std::cerr << "Error: Input file does not exist: " << filepath << "\n";
			return 1;
		}

		auto content = std::make_unique<std::string>(read_file_content(filepath));
		std::string_view source_view = *content;
		source_buffers.push_back(std::move(content));

		Lexer lex{source_view};
		auto tokens = lex.tokenize();

		Parser parser{std::move(tokens)};
		auto prog = parser.parse_program();

		if (parser.has_errors()) {
			has_syntax_errors = true;
			for (const auto &err: parser.errors) {
				std::cerr << filepath << ": " << err << "\n";
			}
		} else {
			parsed_programs.push_back(std::move(prog));
		}
	}

	if (has_syntax_errors) {
		std::cerr << "Compilation aborted due to syntax errors.\n";
		return 1;
	}

	// 2. Gộp toàn bộ khai báo cấp cao (Declarations) từ tất cả các file thành 1 Program AST chung
	auto unified_program = std::make_unique<Program>();
	for (auto &prog: parsed_programs) {
		for (auto &decl: prog->declarations) {
			unified_program->declarations.push_back(std::move(decl));
		}
	}

	// 3. Phân tích Ngữ nghĩa (Semantic Analysis)
	DiagnosticEngine diag;
	Analyzer sema{diag};
	sema.analyze(unified_program.get());

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
		std::cerr << "Compilation aborted due to semantic errors.\n";
		return 1;
	}

	// 4. Sinh mã LLVM (CodeGen)
	CodeGen cg{&sema, base_name};
	if (!cg.setup_target_machine(target_triple)) {
		std::cerr << "Error: Failed to initialize TargetMachine for target: " << target_triple << "\n";
		return 1;
	}

	if (!cg.generate(unified_program.get())) {
		std::cerr << "Error: LLVM IR generation or module verification failed.\n";
		return 1;
	}

	// 5. Xuất kết quả theo chế độ đã chọn
	switch (mode) {
		case OutputMode::IR: {
			std::ofstream out(output_file);
			if (!out.is_open()) {
				std::cerr << "Error: Cannot write to output file: " << output_file << "\n";
				return 1;
			}
			out << cg.dump_ir();
			std::cout << "LLVM IR emitted: " << output_file << "\n";
			break;
		}

		case OutputMode::ASSEMBLY: {
			if (!cg.emit_assembly_file(output_file)) {
				std::cerr << "Error: Failed to emit assembly file: " << output_file << "\n";
				return 1;
			}
			std::cout << "Assembly emitted: " << output_file << "\n";
			break;
		}

		case OutputMode::OBJECT: {
			if (!cg.emit_object_file(output_file)) {
				std::cerr << "Error: Failed to emit object file: " << output_file << "\n";
				return 1;
			}
			std::cout << "Object file emitted: " << output_file << "\n";
			break;
		}

		case OutputMode::EXECUTABLE: {
			std::string tmp_obj = base_name + ".tmp.obj";
			if (!cg.emit_object_file(tmp_obj)) {
				std::cerr << "Error: Failed to emit temporary object file.\n";
				return 1;
			}

			bool linked = CodeGen::link_executable(tmp_obj, output_file);
			std::filesystem::remove(tmp_obj);

			if (!linked) {
				std::cerr << "Error: Linking executable failed.\n";
				return 1;
			}
			std::cout << "Executable created: " << output_file << "\n";
			break;
		}
	}

	return 0;
}
