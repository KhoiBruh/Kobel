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
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

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
		std::cerr << "Không thể mở file output: " << ec.message() << std::endl;
		return false;
	}

	llvm::legacy::PassManager pass;
	if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		std::cerr << "TargetMachine không hỗ trợ sinh file object cho target này" << std::endl;
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
		std::cerr << "Không thể mở file output: " << ec.message() << std::endl;
		return false;
	}

	llvm::legacy::PassManager pass;
	if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
		std::cerr << "TargetMachine không hỗ trợ sinh file assembly cho target này" << std::endl;
		return false;
	}

	pass.run(*module);
	dest.flush();
	return true;
}

bool CodeGen::link_executable(const std::string &obj_filename, const std::string &exe_filename) {
	const std::string cmd = R"(""C:\LLVM\bin\clang.exe" ")" + obj_filename + "\" -o \"" + exe_filename + "\"\"";
	const int ret = std::system(cmd.c_str());
	return ret == 0;
}
