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
#include <optional>
#include <ranges>
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

	std::vector<StringMap<llvm::AllocaInst*>> local_var_scopes;
	std::vector<StringMap<Semantic>> local_type_scopes;

	void push_scope() {
		local_var_scopes.emplace_back();
		local_type_scopes.emplace_back();
	}

	void pop_scope() {
		if (!local_var_scopes.empty()) local_var_scopes.pop_back();
		if (!local_type_scopes.empty()) local_type_scopes.pop_back();
	}

	void clear_scopes() {
		local_var_scopes.clear();
		local_type_scopes.clear();
	}

	void add_local(const std::string_view name, llvm::AllocaInst* alloca, const Semantic& ty) {
		if (local_var_scopes.empty()) push_scope();
		local_var_scopes.back()[std::string(name)] = alloca;
		local_type_scopes.back()[std::string(name)] = ty;
	}

	llvm::AllocaInst* lookup_local_var(const std::string_view name) const {
		for (const auto & local_var_scope : std::views::reverse(local_var_scopes)) {
			if (auto f = local_var_scope.find(name); f != local_var_scope.end()) return f->second;
		}
		return nullptr;
	}

	std::optional<Semantic> lookup_local_type(const std::string_view name) const {
		for (const auto & local_type_scope : std::views::reverse(local_type_scopes)) {
			if (auto f = local_type_scope.find(name); f != local_type_scope.end()) return f->second;
		}
		return std::nullopt;
	}

	StringMap<llvm::GlobalVariable*> global_consts;
	StringMap<llvm::StructType*> struct_types;

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
		init_str_type();
		declare_runtime_functions();
		emit_str_helpers();
	}

	void init_str_type() {
		// str = { data: ptr, len: i64, cap: i64 }
		llvm::Type* str_fields[] = {
			llvm::PointerType::get(*context, 0),  // data
			builder->getInt64Ty(),                  // len
			builder->getInt64Ty()                   // cap
		};
		auto* str_struct = llvm::StructType::create(*context, str_fields, "str");
		struct_types["str"] = str_struct;
	}

	void declare_runtime_functions() {
		auto* ptr_ty = llvm::PointerType::get(*context, 0);
		auto* i64_ty = builder->getInt64Ty();
		auto* i32_ty = builder->getInt32Ty();
		auto* void_ty = builder->getVoidTy();

		// void* malloc(size_t)
		module->getOrInsertFunction("malloc",
			llvm::FunctionType::get(ptr_ty, {i64_ty}, false));
		// void free(void*)
		module->getOrInsertFunction("free",
			llvm::FunctionType::get(void_ty, {ptr_ty}, false));
		// void* memcpy(void* dst, void* src, size_t n)
		module->getOrInsertFunction("memcpy",
			llvm::FunctionType::get(ptr_ty, {ptr_ty, ptr_ty, i64_ty}, false));
		// int memcmp(void* a, void* b, size_t n)
		module->getOrInsertFunction("memcmp",
			llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty, i64_ty}, false));
	}

	void emit_str_helpers() {
		auto* ptr_ty = llvm::PointerType::get(*context, 0);
		auto* i64_ty = builder->getInt64Ty();
		auto* i8_ty = builder->getInt8Ty();
		auto* str_ty = struct_types["str"];

		// __kobel_str_concat(str* a, str* b) -> str
		{
			llvm::FunctionType* fn_ty = llvm::FunctionType::get(str_ty, {ptr_ty, ptr_ty}, false);
			llvm::Function* fn = llvm::Function::Create(fn_ty, llvm::Function::InternalLinkage, "__kobel_str_concat", *module);
			auto* entry = llvm::BasicBlock::Create(*context, "entry", fn);
			builder->SetInsertPoint(entry);

			auto args = fn->arg_begin();
			llvm::Value* a_ptr = &*args++;
			llvm::Value* b_ptr = &*args;

			// Load a.data, a.len
			auto* a_data_ptr = builder->CreateStructGEP(str_ty, a_ptr, 0);
			auto* a_data = builder->CreateLoad(ptr_ty, a_data_ptr, "a.data");
			auto* a_len_ptr = builder->CreateStructGEP(str_ty, a_ptr, 1);
			auto* a_len = builder->CreateLoad(i64_ty, a_len_ptr, "a.len");

			// Load b.data, b.len
			auto* b_data_ptr = builder->CreateStructGEP(str_ty, b_ptr, 0);
			auto* b_data = builder->CreateLoad(ptr_ty, b_data_ptr, "b.data");
			auto* b_len_ptr = builder->CreateStructGEP(str_ty, b_ptr, 1);
			auto* b_len = builder->CreateLoad(i64_ty, b_len_ptr, "b.len");

			// new_len = a.len + b.len
			auto* new_len = builder->CreateAdd(a_len, b_len, "new.len");
			// new_cap = new_len + 1 (for \0)
			auto* new_cap = builder->CreateAdd(new_len, builder->getInt64(1), "new.cap");

			// new_data = malloc(new_cap)
			auto* malloc_fn = module->getFunction("malloc");
			auto* new_data = builder->CreateCall(malloc_fn, {new_cap}, "new.data");

			// memcpy(new_data, a.data, a.len)
			auto* memcpy_fn = module->getFunction("memcpy");
			builder->CreateCall(memcpy_fn, {new_data, a_data, a_len});

			// memcpy(new_data + a.len, b.data, b.len)
			auto* dst_offset = builder->CreateGEP(i8_ty, new_data, a_len, "dst.offset");
			builder->CreateCall(memcpy_fn, {dst_offset, b_data, b_len});

			// new_data[new_len] = '\0'
			auto* null_ptr = builder->CreateGEP(i8_ty, new_data, new_len, "null.pos");
			builder->CreateStore(builder->getInt8(0), null_ptr);

			// Build result str { new_data, new_len, new_cap }
			llvm::Value* result = llvm::UndefValue::get(str_ty);
			result = builder->CreateInsertValue(result, new_data, 0);
			result = builder->CreateInsertValue(result, new_len, 1);
			result = builder->CreateInsertValue(result, new_cap, 2);
			builder->CreateRet(result);
		}

		// __kobel_str_free(str* s) -> void
		{
			llvm::FunctionType* fn_ty = llvm::FunctionType::get(builder->getVoidTy(), {ptr_ty}, false);
			llvm::Function* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, "__kobel_str_free", *module);
			auto* entry = llvm::BasicBlock::Create(*context, "entry", fn);
			auto* free_bb = llvm::BasicBlock::Create(*context, "do.free", fn);
			auto* done_bb = llvm::BasicBlock::Create(*context, "done", fn);
			builder->SetInsertPoint(entry);

			llvm::Value* s_ptr = &*fn->arg_begin();
			auto* cap_ptr = builder->CreateStructGEP(str_ty, s_ptr, 2);
			auto* cap = builder->CreateLoad(i64_ty, cap_ptr, "cap");
			auto* is_owned = builder->CreateICmpUGT(cap, builder->getInt64(0), "is.owned");
			builder->CreateCondBr(is_owned, free_bb, done_bb);

			builder->SetInsertPoint(free_bb);
			auto* data_ptr = builder->CreateStructGEP(str_ty, s_ptr, 0);
			auto* data = builder->CreateLoad(ptr_ty, data_ptr, "data");
			builder->CreateCall(module->getFunction("free"), {data});
			builder->CreateBr(done_bb);

			builder->SetInsertPoint(done_bb);
			builder->CreateRetVoid();
		}
	}

	// ========================================================================
	// 1. Type Mapping
	// ========================================================================

	llvm::Type* get_llvm_type(const Semantic& type) {
		switch (type->kind) {
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

			case SemaType::STR:
				return struct_types["str"];

			case SemaType::POINTER:
			case SemaType::NULL_TYPE:
				return llvm::PointerType::get(*context, 0); // LLVM 23 Opaque Pointer (ptr)

			case SemaType::STRUCT: {
				auto it = struct_types.find(type->struct_name);
				if (it != struct_types.end()) return it->second;
				it = struct_types.find(to_llvm_name(type->struct_name));
				if (it != struct_types.end()) return it->second;
				return llvm::StructType::getTypeByName(*context, to_llvm_name(type->struct_name));
			}

			case SemaType::ENUM: {
				if (type->underlying_type) return get_llvm_type(type->underlying_type);
				return builder->getInt32Ty();
			}

			case SemaType::ARRAY: {
				if (type->element_type) {
					llvm::Type* elem_ty = get_llvm_type(type->element_type);
					return llvm::ArrayType::get(elem_ty, type->array_size);
				}
				return llvm::ArrayType::get(builder->getInt32Ty(), type->array_size);
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
		if (!expr) return analyzer->make_error();
		if (analyzer) {
			auto ty = analyzer->get_expr_type(expr);
			if (!ty->is_error()) return ty;
		}

		if (isa<IdentifierExpr>(expr)) {
			const auto name = as<IdentifierExpr>(expr)->name;
			if (auto opt_ty = lookup_local_type(name)) return *opt_ty;
			if (analyzer) {
				const auto it_c = analyzer->constants.find(name);
				if (it_c != analyzer->constants.end()) return it_c->second.type;
			}
		}
		return analyzer->make_error();
	}

	// ========================================================================
	// 2. Code Generation Function Prototypes
	// ========================================================================

	// Expressions & Addresses (CodeGenExpr.cpp)
	llvm::Value* emit_lvalue(const Expr* expr);
	llvm::Value* emit_expr(const Expr* expr);

	// Statements (CodeGenStmt.cpp)
	void emit_stmt(const Stmt* stmt);

	// Top-level Declarations (CodeGenDecl.cpp)
	void emit_struct_decl(const StructDecl* st);
	void emit_const_decl(const ConstDecl* c);
	void emit_fn_proto(const FnDecl* fn_decl, const std::string& fn_name_override = "");
	void emit_fn_body(const FnDecl* fn_decl, const std::string& fn_name_override = "");
	void emit_fn_decl(const FnDecl* fn_decl, const std::string& fn_name_override = "");
	void emit_extern_block(const ExternBlock* ext);

	// Target Code Generation (CodeGenNative.cpp)
	bool setup_target_machine(const std::string& triple_str = "");
	bool emit_object_file(const std::string& output_filename);
	bool emit_assembly_file(const std::string& output_filename);
	static bool link_executable(const std::string& obj_filename, const std::string& exe_filename);

	// ========================================================================
	// 3. Overall Code Generation & Module Verification
	// ========================================================================

	bool generate(const Program* program) {
		if (!program || !analyzer) return false;

		// 1. Structs
		for (const auto& decl : program->declarations) {
			if (isa<StructDecl>(decl)) {
				emit_struct_decl(as<StructDecl>(decl));
			}
		}

		// 2. Constants
		for (const auto& decl : program->declarations) {
			if (isa<ConstDecl>(decl)) {
				emit_const_decl(as<ConstDecl>(decl));
			}
		}

		// 3. Extern blocks
		for (const auto& decl : program->declarations) {
			if (isa<ExternBlock>(decl)) {
				emit_extern_block(as<ExternBlock>(decl));
			}
		}

		// 4a. Function prototypes (Pass 1: Declare signatures for all functions first)
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl)) {
				const auto* fn = as<FnDecl>(decl);
				std::string mod = analyzer ? analyzer->get_decl_module(fn) : "";
				std::string qual_name = mod.empty() || fn->name == "main" ? std::string(fn->name) : mod + "." + std::string(fn->name);
				emit_fn_proto(fn, to_llvm_name(qual_name));
			}
		}

		// 4b. Function bodies (Pass 2: Generate function bodies; functions can call each other freely)
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl)) {
				const auto* fn = as<FnDecl>(decl);
				std::string mod = analyzer ? analyzer->get_decl_module(fn) : "";
				std::string qual_name = mod.empty() || fn->name == "main" ? std::string(fn->name) : mod + "." + std::string(fn->name);
				emit_fn_body(fn, to_llvm_name(qual_name));
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






