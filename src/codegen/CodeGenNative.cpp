module;

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <llvm/Support/Program.h>

module codegen;

void init_llvm_native_target() {
	static bool initialized = false;
	if (!initialized) {
		llvm::InitializeNativeTarget();
		llvm::InitializeNativeTargetAsmPrinter();
		llvm::InitializeNativeTargetAsmParser();
		initialized = true;
	}
}

bool CodeGen::setup_target_machine(const std::string &triple_str) {
	init_llvm_native_target();

	const auto target_triple = triple_str.empty()
		                           ? "x86_64-pc-windows-msvc"
		                           : triple_str;

	std::string err;
	const llvm::Triple the_triple(target_triple);
	const auto *target = llvm::TargetRegistry::lookupTarget(the_triple, err);
	if (!target) {
		std::cerr << "CodeGen Error lookupTarget: " << err << std::endl;
		return false;
	}

	constexpr std::string cpu = "generic";
	constexpr std::string features;
	const llvm::TargetOptions opt;
	constexpr auto reloc_model = std::optional(llvm::Reloc::PIC_);

	target_machine.reset(
		target->createTargetMachine(
			the_triple,
			cpu,
			features,
			opt,
			reloc_model
		)
	);

	if (!target_machine) return false;

	module->setTargetTriple(the_triple);
	module->setDataLayout(target_machine->createDataLayout());
	return true;
}

bool CodeGen::emit_object_file(const std::string &output_filename) {
	if (!target_machine && !setup_target_machine()) return false;

	std::error_code ec;
	llvm::raw_fd_ostream dest(output_filename, ec, llvm::sys::fs::OF_None);
	if (ec) {
		std::cerr << "Could not open output file: " << ec.message() << std::endl;
		return false;
	}

	llvm::legacy::PassManager pass;
	if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		std::cerr << "TargetMachine does not support emitting object file for this target" << std::endl;
		return false;
	}

	pass.run(*module);
	dest.flush();
	return true;
}

bool CodeGen::emit_assembly_file(const std::string &output_filename) {
	if (!target_machine && !setup_target_machine()) return false;

	std::error_code ec;
	llvm::raw_fd_ostream dest(output_filename, ec, llvm::sys::fs::OF_Text);
	if (ec) {
		std::cerr << "Could not open output file: " << ec.message() << std::endl;
		return false;
	}

	llvm::legacy::PassManager pass;
	if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
		std::cerr << "TargetMachine does not support emitting assembly file for this target" << std::endl;
		return false;
	}

	pass.run(*module);
	dest.flush();
	return true;
}

namespace {
	std::string find_clang_executable() {
		// 1. Check user-specified environment variable
		if (const char* env_kobel_clang = std::getenv("KOBEL_CLANG"); env_kobel_clang && *env_kobel_clang) {
			if (std::filesystem::exists(env_kobel_clang)) return std::string(env_kobel_clang);
		}
		if (const char* env_clang_path = std::getenv("CLANG_PATH"); env_clang_path && *env_clang_path) {
			if (std::filesystem::exists(env_clang_path)) return std::string(env_clang_path);
		}

		// 2. Search system PATH via LLVM Program Support
		if (auto clang_in_path = llvm::sys::findProgramByName("clang"); clang_in_path && !clang_in_path->empty()) {
			return *clang_in_path;
		}
#ifdef _WIN32
		if (auto clang_in_path = llvm::sys::findProgramByName("clang.exe"); clang_in_path && !clang_in_path->empty()) {
			return *clang_in_path;
		}

		// 3. Fallback to standard installation paths on Windows
		const std::vector<std::string> standard_windows_paths = {
			"C:/LLVM/bin/clang.exe",
			R"(C:\LLVM\bin\clang.exe)",
			"C:/Program Files/LLVM/bin/clang.exe",
			R"(C:\Program Files\LLVM\bin\clang.exe)"
		};
		for (const auto& path : standard_windows_paths) {
			if (std::filesystem::exists(path)) return path;
		}
#else
		// 3. Fallback to standard installation paths on POSIX
		const std::vector<std::string> standard_posix_paths = {
			"/usr/bin/clang",
			"/usr/local/bin/clang"
		};
		for (const auto& path : standard_posix_paths) {
			if (std::filesystem::exists(path)) return path;
		}
#endif

		return "";
	}
}

bool CodeGen::link_executable(const std::string &obj_filename, const std::string &exe_filename) {
	const std::string clang_path = find_clang_executable();
	if (clang_path.empty()) {
		std::cerr << "Error: Could not find 'clang' linker executable.\n"
		          << "Please ensure clang is installed and added to PATH, or set the KOBEL_CLANG environment variable.\n";
		return false;
	}

	std::vector<llvm::StringRef> args = {
		clang_path,
		obj_filename,
		"-o",
		exe_filename
	};

	std::string err_msg;
	const int ret = llvm::sys::ExecuteAndWait(clang_path, args, std::nullopt, {}, 0, 0, &err_msg);
	if (ret != 0) {
		std::cerr << "Error: Linking executable failed (exit code " << ret << ")";
		if (!err_msg.empty()) {
			std::cerr << ": " << err_msg;
		}
		std::cerr << "\n";
		return false;
	}

	return true;
}




