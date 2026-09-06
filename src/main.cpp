#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

import lexer;
import parser;
import semantic;
import semantic.analyzer;
import codegen;
import logger;
import ast;

namespace {
	std::string join_module_path(const std::vector<std::string_view>& path) {
		std::string res;
		for (size_t i = 0; i < path.size(); ++i) {
			if (i > 0) res += ".";
			res += path[i];
		}
		return res;
	}

	std::filesystem::path module_to_file_path(const std::vector<std::string_view>& path) {
		std::filesystem::path p;
		for (const auto& part : path) {
			p /= std::string(part);
		}
		return p;
	}

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

	// Determine default output file name if not specified
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
		// Automatically infer mode based on output file extension if not explicitly set
		if (output_file.ends_with(".ll")) mode = OutputMode::IR;
		else if (output_file.ends_with(".s") || output_file.ends_with(".asm")) mode = OutputMode::ASSEMBLY;
		else if (output_file.ends_with(".obj") || output_file.ends_with(".o")) mode = OutputMode::OBJECT;
	}

	DiagnosticEngine diag;

	// 1. Safely load contents of all source files into memory
	// Use std::unique_ptr<std::string> to ensure pointer stability for std::string_view
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

		Parser parser{std::move(tokens), &diag};
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

	// 1b. Automatically find and load imported modules not in input list
	std::unordered_set<std::string> loaded_modules;
	std::unordered_set<std::string> loaded_files;
	std::vector<std::filesystem::path> search_dirs;

	for (const auto &filepath : input_files) {
		std::error_code ec;
		auto can = std::filesystem::canonical(filepath, ec);
		if (!ec) {
			loaded_files.insert(can.string());
			search_dirs.push_back(can.parent_path());
		}
	}
	search_dirs.push_back(std::filesystem::current_path());

	// Collect modules declared in input_files
	for (const auto &prog : parsed_programs) {
		for (const auto &decl : prog->declarations) {
			if (isa<ModuleDecl>(decl.get())) {
				loaded_modules.insert(join_module_path(as<ModuleDecl>(decl.get())->path));
			}
		}
	}

	// Iteratively find and load dependent modules
	bool new_module_loaded = true;
	while (new_module_loaded) {
		new_module_loaded = false;
		std::vector<std::pair<std::vector<std::string_view>, std::string>> pending_imports;

		for (const auto &prog : parsed_programs) {
			for (const auto &decl : prog->declarations) {
				if (isa<UseDecl>(decl.get())) {
					const auto *u = as<UseDecl>(decl.get());
					std::string mod_name = join_module_path(u->path);
					if (!loaded_modules.contains(mod_name)) {
						pending_imports.emplace_back(u->path, mod_name);
					}
				}
			}
		}

		for (const auto &[mod_path, mod_name] : pending_imports) {
			if (loaded_modules.contains(mod_name)) continue;

			auto rel_path = module_to_file_path(mod_path);
			std::filesystem::path found_file;

			for (const auto &dir : search_dirs) {
				auto cand_kb = dir / rel_path;
				cand_kb.replace_extension(".kb");
				if (std::filesystem::exists(cand_kb)) {
					found_file = cand_kb;
					break;
				}
				auto cand_kobel = dir / rel_path;
				cand_kobel.replace_extension(".kobel");
				if (std::filesystem::exists(cand_kobel)) {
					found_file = cand_kobel;
					break;
				}
			}

			if (!found_file.empty()) {
				std::error_code ec;
				auto can = std::filesystem::canonical(found_file, ec);
				std::string can_str = ec ? found_file.string() : can.string();
				if (!loaded_files.contains(can_str)) {
					loaded_files.insert(can_str);
					auto content = std::make_unique<std::string>(read_file_content(found_file.string()));
					std::string_view source_view = *content;
					source_buffers.push_back(std::move(content));

					Lexer lex{source_view};
					auto tokens = lex.tokenize();
					Parser parser{std::move(tokens), &diag};
					auto prog = parser.parse_program();

					if (parser.has_errors()) {
						has_syntax_errors = true;
						for (const auto &err : parser.errors) {
							std::cerr << found_file.string() << ": " << err << "\n";
						}
					} else {
						for (const auto &decl : prog->declarations) {
							if (isa<ModuleDecl>(decl.get())) {
								loaded_modules.insert(join_module_path(as<ModuleDecl>(decl.get())->path));
							}
						}
						loaded_modules.insert(mod_name);
						search_dirs.push_back(found_file.parent_path());
						parsed_programs.push_back(std::move(prog));
						new_module_loaded = true;
					}
				}
			}
		}
	}

	if (has_syntax_errors) {
		std::cerr << "Compilation aborted due to syntax errors in imported modules.\n";
		return 1;
	}

	// 2. Merge all top-level declarations from all files into a unified Program AST
	auto unified_program = std::make_unique<Program>();
	for (auto &prog: parsed_programs) {
		for (auto &decl: prog->declarations) {
			unified_program->declarations.push_back(std::move(decl));
		}
	}

	// 3. Semantic Analysis
	Analyzer sema{diag};
	sema.analyze(unified_program.get());

	if (diag.has_errors()) {
		diag.print_all(std::cerr);
		std::cerr << "Compilation aborted due to semantic errors.\n";
		return 1;
	}

	// 4. LLVM Code Generation
	CodeGen cg{&sema, base_name};
	if (!cg.setup_target_machine(target_triple)) {
		std::cerr << "Error: Failed to initialize TargetMachine for target: " << target_triple << "\n";
		return 1;
	}

	if (!cg.generate(unified_program.get())) {
		std::cerr << "Error: LLVM IR generation or module verification failed.\n";
		return 1;
	}

	// 5. Emit output based on selected mode
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
