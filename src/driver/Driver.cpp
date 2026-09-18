module;

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>
#include <span>

export module driver;

import lexer;
import parser;
import semantic;
import semantic.analyzer;
import codegen;
import logger;
import ast;

export enum class OutputMode {
	EXECUTABLE,
	OBJECT,
	ASSEMBLY,
	IR
};

export using ::OptLevel;

export struct CompilerOptions {
	std::vector<std::string> input_files;
	std::string output_file;
	std::string target_triple = "x86_64-pc-windows-msvc";
	OutputMode mode = OutputMode::EXECUTABLE;
	bool explicit_mode = false;
	OptLevel opt_level = OptLevel::O0;
	std::vector<std::filesystem::path> custom_search_dirs;
};

export class Driver {
public:
	explicit Driver(CompilerOptions options, std::ostream &out = std::cout, std::ostream &err = std::cerr)
		: options_(std::move(options)), out_(out), err_(err) {
	}

	int run();

	const CompilerOptions &options() const { return options_; }
	CompilerOptions &options() { return options_; }
	const DiagnosticEngine &diagnostics() const { return diag_; }

private:
	CompilerOptions options_;
	std::ostream &out_;
	std::ostream &err_;
	DiagnosticEngine diag_;

	static std::string read_file_content(const std::string &path);

	static std::filesystem::path module_to_file_path(const std::span<std::string_view> &path);

	bool resolve_dependencies(
		std::vector<std::unique_ptr<std::string> > &source_buffers,
		std::vector<Program *> &parsed_programs,
		std::vector<std::unique_ptr<Parser> > &parsers
	);

	bool emit_output(CodeGen &cg, const std::string &base_name);
};

std::string Driver::read_file_content(const std::string &path) {
	const std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file.is_open()) return "";
	std::ostringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

std::filesystem::path Driver::module_to_file_path(const std::span<std::string_view> &path) {
	std::filesystem::path p;
	for (const auto &part: path) {
		p /= std::string(part);
	}
	return p;
}

bool Driver::resolve_dependencies(
	std::vector<std::unique_ptr<std::string> > &source_buffers,
	std::vector<Program *> &parsed_programs,
	std::vector<std::unique_ptr<Parser> > &parsers
) {
	StringSet loaded_modules;
	StringSet loaded_files;
	std::vector<std::filesystem::path> search_dirs = options_.custom_search_dirs;

	for (const auto &filepath: options_.input_files) {
		std::error_code ec;
		auto can = std::filesystem::canonical(filepath, ec);
		if (!ec) {
			loaded_files.insert(can.string());
			search_dirs.push_back(can.parent_path());
		}
	}
	search_dirs.push_back(std::filesystem::current_path());

	// Collect modules declared in input_files
	for (const auto &prog: parsed_programs) {
		for (const auto &decl: prog->declarations) {
			if (isa<ModuleDecl>(decl)) {
				loaded_modules.insert(std::string(as<ModuleDecl>(decl)->full_path));
			}
		}
	}

	// Iteratively find and load dependent modules
	bool has_syntax_errors = false;
	bool new_module_loaded = true;
	while (new_module_loaded) {
		new_module_loaded = false;
		std::vector<std::pair<std::span<std::string_view>, std::string_view> > pending_imports;

		for (const auto &prog: parsed_programs) {
			for (const auto &decl: prog->declarations) {
				if (isa<UseDecl>(decl)) {
					const auto *u = as<UseDecl>(decl);
					if (!u->full_path.empty() && !loaded_modules.contains(u->full_path)) {
						pending_imports.emplace_back(u->path, u->full_path);
					}
				}
			}
		}

		for (const auto &[mod_path, mod_name]: pending_imports) {
			if (mod_name.empty() || loaded_modules.contains(mod_name)) continue;

			auto rel_path = module_to_file_path(mod_path);
			std::filesystem::path found_file;

			for (const auto &dir: search_dirs) {
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
					auto parser = std::make_unique<Parser>(std::move(tokens), &diag_);
					auto prog = parser->parse_program();

					if (parser->has_errors()) {
						has_syntax_errors = true;
						for (const auto &err: parser->errors) {
							err_ << found_file.string() << ": " << err << "\n";
						}
					} else {
						for (const auto &decl: prog->declarations) {
							if (isa<ModuleDecl>(decl)) {
								loaded_modules.insert(std::string(as<ModuleDecl>(decl)->full_path));
							}
						}
						loaded_modules.insert(std::string(mod_name));
						search_dirs.push_back(found_file.parent_path());
						parsed_programs.push_back(prog);
						parsers.push_back(std::move(parser));
						new_module_loaded = true;
					}
				} else {
					loaded_modules.insert(std::string(mod_name));
				}
			}
		}
	}

	if (has_syntax_errors) {
		err_ << "Compilation aborted due to syntax errors in imported modules.\n";
		return false;
	}

	return true;
}

bool Driver::emit_output(CodeGen &cg, const std::string &base_name) {
	switch (options_.mode) {
		case OutputMode::IR: {
			std::ofstream out_file(options_.output_file);
			if (!out_file.is_open()) {
				err_ << "Error: Cannot write to output file: " << options_.output_file << "\n";
				return false;
			}
			out_file << cg.dump_ir();
			out_ << "LLVM IR emitted: " << options_.output_file << "\n";
			return true;
		}

		case OutputMode::ASSEMBLY: {
			if (!cg.emit_assembly_file(options_.output_file)) {
				err_ << "Error: Failed to emit assembly file: " << options_.output_file << "\n";
				return false;
			}
			out_ << "Assembly emitted: " << options_.output_file << "\n";
			return true;
		}

		case OutputMode::OBJECT: {
			if (!cg.emit_object_file(options_.output_file)) {
				err_ << "Error: Failed to emit object file: " << options_.output_file << "\n";
				return false;
			}
			out_ << "Object file emitted: " << options_.output_file << "\n";
			return true;
		}

		case OutputMode::EXECUTABLE: {
			std::string tmp_obj = base_name + ".tmp.obj";
			if (!cg.emit_object_file(tmp_obj)) {
				err_ << "Error: Failed to emit temporary object file.\n";
				return false;
			}

			bool linked = CodeGen::link_executable(tmp_obj, options_.output_file);
			std::error_code ec;
			std::filesystem::remove(tmp_obj, ec);

			if (!linked) {
				err_ << "Error: Linking executable failed.\n";
				return false;
			}
			out_ << "Executable created: " << options_.output_file << "\n";
			return true;
		}
	}
	return false;
}

int Driver::run() {
	if (options_.input_files.empty()) {
		err_ << "Error: No input files provided.\n";
		return 1;
	}

	// Determine default output file name if not specified
	auto base_name = std::filesystem::path(options_.input_files[0]).stem().string();
	if (options_.output_file.empty()) {
		switch (options_.mode) {
			case OutputMode::EXECUTABLE:
				options_.output_file = base_name + ".exe";
				break;
			case OutputMode::OBJECT:
				options_.output_file = base_name + ".obj";
				break;
			case OutputMode::ASSEMBLY:
				options_.output_file = base_name + ".s";
				break;
			case OutputMode::IR:
				options_.output_file = base_name + ".ll";
				break;
		}
	} else if (!options_.explicit_mode) {
		// Automatically infer mode based on output file extension if not explicitly set
		if (options_.output_file.ends_with(".ll")) options_.mode = OutputMode::IR;
		else if (options_.output_file.ends_with(".s") || options_.output_file.ends_with(".asm"))
			options_.mode = OutputMode::ASSEMBLY;
		else if (options_.output_file.ends_with(".obj") || options_.output_file.ends_with(".o"))
			options_.mode = OutputMode::OBJECT;
	}

	// 1. Safely load contents of all source files into memory
	// Use std::unique_ptr<std::string> to ensure pointer stability for std::string_view
	std::vector<std::unique_ptr<std::string> > source_buffers;
	source_buffers.reserve(options_.input_files.size());

	std::vector<Program *> parsed_programs;
	std::vector<std::unique_ptr<Parser> > parsers;
	parsed_programs.reserve(options_.input_files.size());

	bool has_syntax_errors = false;

	for (const auto &filepath: options_.input_files) {
		if (!std::filesystem::exists(filepath)) {
			err_ << "Error: Input file does not exist: " << filepath << "\n";
			return 1;
		}

		auto content = std::make_unique<std::string>(read_file_content(filepath));
		std::string_view source_view = *content;
		source_buffers.push_back(std::move(content));

		Lexer lex{.src = source_view};
		auto tokens = lex.tokenize();

		auto parser = std::make_unique<Parser>(std::move(tokens), &diag_);
		auto prog = parser->parse_program();

		if (parser->has_errors()) {
			has_syntax_errors = true;
			for (const auto &err: parser->errors) {
				err_ << filepath << ": " << err << "\n";
			}
		} else {
			parsed_programs.push_back(std::move(prog));
			parsers.push_back(std::move(parser));
		}
	}

	if (has_syntax_errors) {
		err_ << "Compilation aborted due to syntax errors.\n";
		return 1;
	}

	// 1b. Automatically find and load imported modules not in input list
	if (!resolve_dependencies(source_buffers, parsed_programs, parsers)) {
		return 1;
	}

	// 2. Merge all top-level declarations from all files into a unified Program AST
	auto unified_program = parsers.front()->arena.alloc<Program>();
	std::vector<Decl *> all_decls;
	for (auto &prog: parsed_programs) {
		for (auto &decl: prog->declarations) {
			all_decls.push_back(decl);
		}
	}
	unified_program->declarations = parsers.front()->arena.alloc_span<Decl *>(all_decls);

	// 3. Semantic Analysis
	Analyzer sema{diag_};
	sema.analyze(unified_program);

	if (diag_.has_errors()) {
		diag_.print_all(err_);
		err_ << "Compilation aborted due to semantic errors.\n";
		return 1;
	}

	// 4. LLVM Code Generation
	CodeGen cg{&sema, base_name};
	if (!cg.setup_target_machine(options_.target_triple)) {
		err_ << "Error: Failed to initialize TargetMachine for target: " << options_.target_triple << "\n";
		return 1;
	}

	if (!cg.generate(unified_program)) {
		err_ << "Error: LLVM IR generation or module verification failed.\n";
		return 1;
	}

	if (!cg.optimize(options_.opt_level)) {
		err_ << "Error: LLVM optimization pipeline failed.\n";
		return 1;
	}

	// 5. Emit output based on selected mode
	return emit_output(cg, base_name) ? 0 : 1;
}
