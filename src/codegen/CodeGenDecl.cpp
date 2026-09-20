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

#include <iostream>
#include <string>
#include <vector>

module codegen;

import token;
import ast;
import semantic;
import semantic.symbol;
import semantic.analyzer;

// ============================================================================
// Code Generation for Declarations
// ============================================================================

void CodeGen::emit_struct_decl(const StructDecl *st) {
	if (!st->type_params.empty()) return; // Skip generic struct templates (monomorphized on demand)

	std::string mod = analyzer ? analyzer->get_decl_module(st) : "";
	std::string qual_name = mod.empty() ? std::string(st->name) : mod + "." + std::string(st->name);
	std::string llvm_st_name = to_llvm_name(qual_name);

	if (auto it = struct_types.find(llvm_st_name); it != struct_types.end() && it->second != nullptr && !it->second->isOpaque()) {
		if (analyzer && (analyzer->structs.contains(qual_name) || analyzer->structs.contains(llvm_st_name))) {
			const auto &sym = analyzer->structs.contains(qual_name)
			                      ? analyzer->structs.at(qual_name)
			                      : analyzer->structs.at(llvm_st_name);
			for (const auto *method: sym.method_decls) {
				emit_fn_decl(method, llvm_st_name + "_" + std::string(method->name));
			}
		}

		if (analyzer && analyzer->struct_default_methods.contains(qual_name)) {
			for (const auto &inh: analyzer->struct_default_methods.at(qual_name)) {
				emit_fn_decl(inh.fn_decl, llvm_st_name + "_" + inh.method_name);
			}
		}
		return;
	}

	std::vector<llvm::Type *> field_types;

	for (const auto &field: st->fields) {
		auto sema_ty = analyzer->resolve_type(field.type);
		field_types.push_back(get_llvm_type(sema_ty));
	}

	llvm::StructType *struct_ty = llvm::StructType::getTypeByName(*context, llvm_st_name);
	if (!struct_ty) {
		struct_ty = llvm::StructType::create(*context, field_types, llvm_st_name);
	} else if (struct_ty->isOpaque()) {
		struct_ty->setBody(field_types);
	}
	struct_types[llvm_st_name] = struct_ty;
	struct_types[qual_name] = struct_ty;
	struct_types[std::string(st->name)] = struct_ty;

	if (analyzer && (analyzer->structs.contains(qual_name) || analyzer->structs.contains(llvm_st_name))) {
		const auto &sym = analyzer->structs.contains(qual_name)
		                      ? analyzer->structs.at(qual_name)
		                      : analyzer->structs.at(llvm_st_name);
		for (const auto *method: sym.method_decls) {
			emit_fn_decl(method, llvm_st_name + "_" + std::string(method->name));
		}
	}

	if (analyzer && analyzer->struct_default_methods.contains(qual_name)) {
		for (const auto &inh: analyzer->struct_default_methods.at(qual_name)) {
			emit_fn_decl(inh.fn_decl, llvm_st_name + "_" + inh.method_name);
		}
	}
}

void CodeGen::emit_instantiated_struct(const std::string &inst_name) {
	std::string llvm_st_name = to_llvm_name(inst_name);
	if (auto it = struct_types.find(llvm_st_name); it != struct_types.end() && it->second != nullptr) return;
	if (!analyzer) return;
	const StructSymbol *sym_ptr = nullptr;
	if (auto it = analyzer->structs.find(inst_name); it != analyzer->structs.end()) {
		sym_ptr = &it->second;
	} else if (auto it = analyzer->structs.find(llvm_st_name); it != analyzer->structs.end()) {
		sym_ptr = &it->second;
	}
	if (!sym_ptr) return;

	llvm::StructType *struct_ty = llvm::StructType::getTypeByName(*context, llvm_st_name);
	if (!struct_ty) {
		struct_ty = llvm::StructType::create(*context, llvm_st_name);
	}
	struct_types[llvm_st_name] = struct_ty;
	struct_types[inst_name] = struct_ty;

	const auto &sym = *sym_ptr;
	std::vector<llvm::Type *> field_types;
	for (const auto &f_name: sym.field_order) {
		const auto &f_type = sym.field_types.at(f_name);
		field_types.push_back(get_llvm_type(f_type));
	}

	struct_ty->setBody(field_types);

	for (const auto *method: sym.method_decls) {
		std::string mangled = llvm_st_name + "_" + std::string(method->name);
		emit_fn_proto(method, mangled);
	}
	if (analyzer && analyzer->struct_default_methods.contains(inst_name)) {
		for (const auto &inh: analyzer->struct_default_methods.at(inst_name)) {
			std::string mangled = llvm_st_name + "_" + inh.method_name;
			emit_fn_proto(inh.fn_decl, mangled);
		}
	}
}

void CodeGen::emit_const_decl(const ConstDecl *c) {
	std::string mod = analyzer ? analyzer->get_decl_module(c) : "";
	std::string qual_name = mod.empty() ? std::string(c->name) : mod + "." + std::string(c->name);
	std::string llvm_name = to_llvm_name(qual_name);

	const auto sema_ty = analyzer->resolve_type(c->type);
	llvm::Type *llvm_ty = get_llvm_type(sema_ty);

	llvm::Constant *init_const = nullptr;
	if (c->value && isa<LiteralExpr>(c->value)) {
		const auto *lit = as<LiteralExpr>(c->value);
		if (lit->literal_kind == LiteralKind::INT) {
			const int64_t val = parse_kobel_int(lit->raw_text);
			init_const = llvm::ConstantInt::get(llvm_ty, val);
		}
	}
	if (!init_const) {
		init_const = llvm::Constant::getNullValue(llvm_ty);
	}

	auto *gv = new llvm::GlobalVariable(
		*module,
		llvm_ty,
		true, // constant
		llvm::GlobalValue::InternalLinkage,
		init_const,
		llvm_name
	);
	global_consts[llvm_name] = gv;
	global_consts[qual_name] = gv;
	global_consts[std::string(c->name)] = gv;
}

void CodeGen::emit_fn_proto(const FnDecl *fn_decl, const std::string &fn_name_override) {
	if (fn_decl && !fn_decl->type_params.empty() && fn_name_override.empty()) return;
	const auto name = fn_name_override.empty() ? std::string(fn_decl->name) : fn_name_override;
	llvm::Function *fn = module->getFunction(name);

	if (!fn) {
		std::vector<llvm::Type *> param_types;
		llvm::Type *ret_type = nullptr;
		if (analyzer && analyzer->functions.contains(name)) {
			const auto &sym = analyzer->functions.at(name);
			for (const auto &pt: sym.param_types) {
				param_types.push_back(get_llvm_type(pt));
			}
			ret_type = get_llvm_type(sym.return_type);
		} else {
			for (const auto &p: fn_decl->params) {
				auto sema_ty = analyzer->resolve_type(p.type);
				param_types.push_back(get_llvm_type(sema_ty));
			}
			auto ret_sema_ty = fn_decl->return_type
								   ? analyzer->resolve_type(fn_decl->return_type)
								   : analyzer->make_primitive(SemaType::VOID);
			ret_type = get_llvm_type(ret_sema_ty);
		}

		llvm::FunctionType *fn_type = llvm::FunctionType::get(ret_type, param_types, false);
		llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, *module);
	}
}

void CodeGen::emit_fn_body(const FnDecl *fn_decl, const std::string &fn_name_override) {
	if (!fn_decl->body) return; // Prototype extern
	if (!fn_decl->type_params.empty() && fn_name_override.empty()) return;

	const auto name = fn_name_override.empty() ? std::string(fn_decl->name) : fn_name_override;
	llvm::Function *fn = module->getFunction(name);
	if (!fn) {
		emit_fn_proto(fn_decl, name);
		fn = module->getFunction(name);
	}
	if (!fn || !fn->empty()) return; // Function body already emitted

	std::string old_mod;
	if (analyzer) {
		old_mod = analyzer->current_module;
		std::string mod = analyzer->decl_modules.contains(fn_decl)
							  ? analyzer->decl_modules.at(fn_decl)
							  : analyzer->get_decl_module(fn_decl);
		if (!mod.empty()) analyzer->current_module = mod;
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context, "entry", fn);
	builder->SetInsertPoint(entry);
	clear_scopes();
	push_scope();

	// Create alloca for function parameters
	unsigned idx = 0;
	for (auto &arg: fn->args()) {
		const auto param_name = std::string(fn_decl->params[idx].name);
		arg.setName(param_name);
		llvm::AllocaInst * alloca = create_entry_block_alloca(fn, arg.getType(), param_name);
		builder->CreateStore(&arg, alloca);
		Semantic param_sema = analyzer && analyzer->functions.contains(name)
								  ? analyzer->functions.at(name).param_types[idx]
								  : analyzer->resolve_type(fn_decl->params[idx].type);
		add_local(param_name, alloca, param_sema);
		idx++;
	}

	emit_stmt(fn_decl->body);

	// If the last basic block lacks a terminator, insert an automatic return or unreachable
	if (auto *cur_bb = builder->GetInsertBlock(); cur_bb && !cur_bb->hasTerminator()) {
		if (fn->getReturnType()->isVoidTy()) {
			builder->CreateRetVoid();
		} else if (name == "main" && fn->getReturnType()->isIntegerTy(32)) {
			builder->CreateRet(builder->getInt32(0));
		} else {
			builder->CreateUnreachable();
		}
	}
	if (analyzer) {
		analyzer->current_module = old_mod;
	}
}

void CodeGen::emit_fn_decl(const FnDecl *fn_decl, const std::string &fn_name_override) {
	emit_fn_proto(fn_decl, fn_name_override);
	emit_fn_body(fn_decl, fn_name_override);
}

void CodeGen::emit_extern_block(const ExternBlock *ext) {
	for (const auto &fn: ext->declarations) {
		emit_fn_decl(fn);
	}
}
