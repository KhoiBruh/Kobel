#include <iostream>
#include <string>
#include <string_view>
#include <vector>

import driver;

namespace {
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
}

int main(const int argc, char *argv[]) {
	if (argc < 2) {
		print_help(argv[0]);
		return 1;
	}

	CompilerOptions options;

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
				options.output_file = argv[++i];
			} else {
				std::cerr << "Error: Missing argument after '-o'\n";
				return 1;
			}
		} else if (arg == "-c" || arg == "--emit-obj") {
			options.mode = OutputMode::OBJECT;
			options.explicit_mode = true;
		} else if (arg == "-S" || arg == "--emit-asm") {
			options.mode = OutputMode::ASSEMBLY;
			options.explicit_mode = true;
		} else if (arg == "--emit-ir") {
			options.mode = OutputMode::IR;
			options.explicit_mode = true;
		} else if (arg == "--target") {
			if (i + 1 < argc) {
				options.target_triple = argv[++i];
			} else {
				std::cerr << "Error: Missing argument after '--target'\n";
				return 1;
			}
		} else if (arg.starts_with('-')) {
			std::cerr << "Error: Unknown option '" << arg << "'\n";
			return 1;
		} else {
			options.input_files.emplace_back(arg);
		}
	}

	Driver driver{options};
	return driver.run();
}
