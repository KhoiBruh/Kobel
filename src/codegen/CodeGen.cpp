module;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module codegen;

import token;
import ast;
import logger;
import semantic;
import semantic.symbol;
import semantic.analyzer;

export struct CodeGen {
	std::unique_ptr<llvm::LLVMContext> context;
	std::unique_ptr<llvm::Module> module;
	std::unique_ptr<llvm::IRBuilder<>> builder;
	Analyzer* analyzer = nullptr;

	std::unordered_map<std::string, llvm::AllocaInst*> local_vars;
	std::unordered_map<std::string, Semantic> local_types;
	std::unordered_map<std::string, llvm::GlobalVariable*> global_consts;
	std::unordered_map<std::string, llvm::StructType*> struct_types;

	struct LoopContext {
		llvm::BasicBlock* cond_bb;
		llvm::BasicBlock* after_bb;
	};
	std::vector<LoopContext> loop_stack;

	std::unique_ptr<llvm::TargetMachine> target_machine;

	explicit CodeGen(Analyzer* sema, const std::string_view module_name = "kobel_module")
		: context(std::make_unique<llvm::LLVMContext>()),
		  module(std::make_unique<llvm::Module>(std::string(module_name), *context)),
		  builder(std::make_unique<llvm::IRBuilder<>>(*context)),
		  analyzer(sema) {
		setup_target_machine("x86_64-pc-windows-msvc");
	}

	// ========================================================================
	// 1. Ánh xạ Kiểu Dữ liệu (Type Mapping)
	// ========================================================================

	llvm::Type* get_llvm_type(const Semantic& type) {
		switch (type.kind) {
			case SemaType::I8:
			case SemaType::U8:
			case SemaType::CHAR:
				return builder->getInt8Ty();

			case SemaType::I16:
			case SemaType::U16:
				return builder->getInt16Ty();

			case SemaType::I32:
			case SemaType::U32:
				return builder->getInt32Ty();

			case SemaType::I64:
			case SemaType::U64:
			case SemaType::ISZ:
			case SemaType::USZ:
				return builder->getInt64Ty();

			case SemaType::BOOL:
				return builder->getInt1Ty();

			case SemaType::VOID:
				return builder->getVoidTy();

			case SemaType::POINTER:
			case SemaType::NULL_TYPE:
				return llvm::PointerType::get(*context, 0); // LLVM 23 Opaque Pointer (ptr)

			case SemaType::STRUCT: {
				auto it = struct_types.find(type.struct_name);
				if (it != struct_types.end()) return it->second;
				return llvm::StructType::getTypeByName(*context, type.struct_name);
			}

			default:
				return builder->getInt32Ty();
		}
	}

	llvm::AllocaInst* create_entry_block_alloca(llvm::Function* fn, llvm::Type* type, const std::string_view name) {
		llvm::IRBuilder tmp_builder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
		return tmp_builder.CreateAlloca(type, nullptr, std::string(name));
	}

	Semantic get_sema_type(const Expr* expr) {
		if (!expr) return Semantic::make_error();
		if (analyzer) {
			auto ty = analyzer->get_expr_type(expr);
			if (!ty.is_error()) return ty;
		}

		if (isa<IdentifierExpr>(expr)) {
			const auto name = std::string(as<IdentifierExpr>(expr)->name);
			if (
				const auto it = local_types.find(name);
				it != local_types.end()
			) return it->second;
			if (analyzer) {
				const auto it_c = analyzer->constants.find(name);
				if (it_c != analyzer->constants.end()) return it_c->second.type;
			}
		}
		return Semantic::make_error();
	}

	// ========================================================================
	// 2. Chữ ký các hàm sinh mã (Prototypes)
	// ========================================================================

	// Biểu thức & Địa chỉ (CodeGenExpr.cpp)
	llvm::Value* emit_lvalue(const Expr* expr);
	llvm::Value* emit_expr(const Expr* expr);

	// Câu lệnh (CodeGenStmt.cpp)
	void emit_stmt(const Stmt* stmt);

	// Khai báo cấp cao (CodeGenDecl.cpp)
	void emit_struct_decl(const StructDecl* st);
	void emit_const_decl(const ConstDecl* c);
	void emit_fn_proto(const FnDecl* fn_decl);
	void emit_fn_body(const FnDecl* fn_decl);
	void emit_fn_decl(const FnDecl* fn_decl);
	void emit_extern_block(const ExternBlock* ext);

	// Phát sinh mã máy đích (CodeGenNative.cpp)
	bool setup_target_machine(const std::string& triple_str = "");
	bool emit_object_file(const std::string& output_filename);
	bool emit_assembly_file(const std::string& output_filename);
	static bool link_executable(const std::string& obj_filename, const std::string& exe_filename);

	// ========================================================================
	// 3. Tổng thể Sinh mã & Kiểm định Module
	// ========================================================================

	bool generate(const Program* program) {
		if (!program || !analyzer) return false;

		// 1. Structs
		for (const auto& decl : program->declarations) {
			if (isa<StructDecl>(decl.get())) {
				emit_struct_decl(as<StructDecl>(decl.get()));
			}
		}

		// 2. Constants
		for (const auto& decl : program->declarations) {
			if (isa<ConstDecl>(decl.get())) {
				emit_const_decl(as<ConstDecl>(decl.get()));
			}
		}

		// 3. Extern blocks
		for (const auto& decl : program->declarations) {
			if (isa<ExternBlock>(decl.get())) {
				emit_extern_block(as<ExternBlock>(decl.get()));
			}
		}

		// 4a. Function prototypes (Pass 1: Khai báo chữ ký toàn bộ hàm trước)
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl.get())) {
				emit_fn_proto(as<FnDecl>(decl.get()));
			}
		}

		// 4b. Function bodies (Pass 2: Sinh thân hàm, các hàm có thể gọi chéo nhau tự do)
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl.get())) {
				emit_fn_body(as<FnDecl>(decl.get()));
			}
		}

		return verify();
	}

	bool verify() const {
		std::string err_str;
		llvm::raw_string_ostream os(err_str);
		return !llvm::verifyModule(*module, &os);
	}

	std::string dump_ir() const {
		std::string ir_str;
		llvm::raw_string_ostream os(ir_str);
		module->print(os, nullptr);
		return ir_str;
	}
};
