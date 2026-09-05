module;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <string>
#include <vector>

module codegen;

import token;
import ast;
import semantic;
import semantic.symbol;
import semantic.analyzer;

// ============================================================================
// Sinh mã Khai báo Cấp cao (Declarations)
// ============================================================================

void CodeGen::emit_struct_decl(const StructDecl* st) {
	const auto name = std::string(st->name);
	std::vector<llvm::Type*> field_types;

	for (const auto&[name, type] : st->fields) {
		auto sema_ty = analyzer->resolve_type(type.get());
		field_types.push_back(get_llvm_type(sema_ty));
	}

	llvm::StructType* struct_ty = llvm::StructType::create(*context, field_types, name);
	struct_types[name] = struct_ty;
}

void CodeGen::emit_const_decl(const ConstDecl* c) {
	const auto name = std::string(c->name);
	const auto sema_ty = analyzer->resolve_type(c->type.get());
	llvm::Type* llvm_ty = get_llvm_type(sema_ty);

	llvm::Constant* init_const = nullptr;
	if (c->value && isa<LiteralExpr>(c->value.get())) {
		const auto* lit = as<LiteralExpr>(c->value.get());
		if (lit->literal_kind == LiteralKind::INT) {
			const int64_t val = std::stoll(std::string(lit->raw_text));
			init_const = llvm::ConstantInt::get(llvm_ty, val);
		}
	}
	if (!init_const) {
		init_const = llvm::Constant::getNullValue(llvm_ty);
	}

	auto* gv = new llvm::GlobalVariable(
		*module,
		llvm_ty,
		true, // constant
		llvm::GlobalValue::InternalLinkage,
		init_const,
		name
	);
	global_consts[name] = gv;
}

void CodeGen::emit_fn_decl(const FnDecl* fn_decl) {
	const auto name = std::string(fn_decl->name);
	llvm::Function* fn = module->getFunction(name);

	if (!fn) {
		std::vector<llvm::Type*> param_types;
		for (const auto&[name, type] : fn_decl->params) {
			auto sema_ty = analyzer->resolve_type(type.get());
			param_types.push_back(get_llvm_type(sema_ty));
		}
		auto ret_sema_ty = fn_decl->return_type
			? analyzer->resolve_type(fn_decl->return_type.get())
			: Semantic::make_primitive(SemaType::VOID);
		llvm::Type* ret_type = get_llvm_type(ret_sema_ty);

		llvm::FunctionType* fn_type = llvm::FunctionType::get(ret_type, param_types, false);
		fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, *module);
	}

	if (!fn_decl->body) return; // Prototype extern

	llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", fn);
	builder->SetInsertPoint(entry);
	local_vars.clear();
	local_types.clear();

	// Tạo alloca cho tham số hàm
	unsigned idx = 0;
	for (auto& arg : fn->args()) {
		const auto param_name = std::string(fn_decl->params[idx].name);
		arg.setName(param_name);
		llvm::AllocaInst* alloca = create_entry_block_alloca(fn, arg.getType(), param_name);
		builder->CreateStore(&arg, alloca);
		local_vars[param_name] = alloca;
		local_types[param_name] = analyzer->resolve_type(fn_decl->params[idx].type.get());
		idx++;
	}

	emit_stmt(fn_decl->body.get());

	// Nếu block cuối chưa có lệnh kết thúc terminator, tự động chèn return
	if (auto* cur_bb = builder->GetInsertBlock(); cur_bb && !cur_bb->hasTerminator()) {
		if (fn->getReturnType()->isVoidTy()) {
			builder->CreateRetVoid();
		} else if (fn->getReturnType()->isIntegerTy(32)) {
			builder->CreateRet(builder->getInt32(0));
		}
	}
}

void CodeGen::emit_extern_block(const ExternBlock* ext) {
	for (const auto& fn : ext->declarations) {
		emit_fn_decl(fn.get());
	}
}
