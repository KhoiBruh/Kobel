module;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
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
// Code Generation for Statements & Control Flow
// ============================================================================

void CodeGen::emit_stmt(const Stmt* stmt) {
	if (!stmt) return;

	if (auto* cur_bb = builder->GetInsertBlock(); cur_bb && cur_bb->hasTerminator()) {
		return;
	}

	// 1. Variable declaration: val / var
	if (isa<VarDeclStmt>(stmt)) {
		const auto* v = as<VarDeclStmt>(stmt);
		const auto name = std::string(v->name);
		auto sema_ty = analyzer->resolve_type(v->type_annotation);
		if (sema_ty->is_array() && sema_ty->array_size == 0 && v->initializer) {
			auto init_sema = get_sema_type(v->initializer);
			if (init_sema->is_array()) {
				sema_ty->array_size = (init_sema->array_size);
			}
		}
		llvm::Type* var_type = get_llvm_type(sema_ty);

		llvm::Function* fn = builder->GetInsertBlock()->getParent();
		llvm::AllocaInst* alloca = create_entry_block_alloca(fn, var_type, name);
		add_local(v->name, alloca, sema_ty);

		if (v->initializer) {
			if (isa<ArrayLiteralExpr>(v->initializer)) {
				const auto* arr_lit = as<ArrayLiteralExpr>(v->initializer);
				for (size_t i = 0; i < arr_lit->elements.size(); ++i) {
					llvm::Value* elem_val = emit_expr(arr_lit->elements[i]);
					llvm::Value* elem_ptr = builder->CreateGEP(
						var_type, alloca,
						{builder->getInt32(0), builder->getInt32(static_cast<int32_t>(i))},
						name + "_init"
					);
					builder->CreateStore(elem_val, elem_ptr);
				}
			} else if (isa<CallExpr>(v->initializer) &&
			           isa<IdentifierExpr>(as<CallExpr>(v->initializer)->callee) &&
			           analyzer && analyzer->structs.contains(as<IdentifierExpr>(as<CallExpr>(v->initializer)->callee)->name)) {
				const auto* call = as<CallExpr>(v->initializer);
				for (size_t i = 0; i < call->args.size(); ++i) {
					llvm::Value* arg_val = emit_expr(call->args[i]);
					llvm::Value* field_ptr = builder->CreateStructGEP(var_type, alloca, static_cast<unsigned>(i), name + "_f");
					builder->CreateStore(arg_val, field_ptr);
				}
			} else {
				llvm::Value* init_val = emit_expr(v->initializer);
				builder->CreateStore(init_val, alloca);
			}
		}
		return;
	}

	// 2. Block: { ... }
	if (isa<BlockStmt>(stmt)) {
		const auto* b = as<BlockStmt>(stmt);
		push_scope();
		for (const auto& s : b->statements) {
			emit_stmt(s);
		}
		pop_scope();
		return;
	}

	// 3. If statement: if (cond) { ... } else { ... }
	if (isa<IfStmt>(stmt)) {
		const auto* i = as<IfStmt>(stmt);
		llvm::Value* cond = emit_expr(i->condition);
		llvm::Function* fn = builder->GetInsertBlock()->getParent();

		llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(*context, "then", fn);
		llvm::BasicBlock* else_bb = i->else_branch ? llvm::BasicBlock::Create(*context, "else") : nullptr;
		llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(*context, "if_merge");

		builder->CreateCondBr(cond, then_bb, i->else_branch ? else_bb : merge_bb);

		// Then block
		builder->SetInsertPoint(then_bb);
		emit_stmt(i->then_branch);
		if (!builder->GetInsertBlock()->hasTerminator()) {
			builder->CreateBr(merge_bb);
		}

		// Else block
		if (i->else_branch) {
			fn->insert(fn->end(), else_bb);
			builder->SetInsertPoint(else_bb);
			emit_stmt(i->else_branch);
			if (!builder->GetInsertBlock()->hasTerminator()) {
				builder->CreateBr(merge_bb);
			}
		}

		// Merge block
		fn->insert(fn->end(), merge_bb);
		builder->SetInsertPoint(merge_bb);
		return;
	}

	// 4. While loop: while (cond) { ... }
	if (isa<WhileStmt>(stmt)) {
		const auto* w = as<WhileStmt>(stmt);
		llvm::Function* fn = builder->GetInsertBlock()->getParent();

		llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(*context, "while_cond", fn);
		llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(*context, "while_body", fn);
		llvm::BasicBlock* after_bb = llvm::BasicBlock::Create(*context, "while_after", fn);

		builder->CreateBr(cond_bb);

		// Cond block
		builder->SetInsertPoint(cond_bb);
		llvm::Value* cond = emit_expr(w->condition);
		builder->CreateCondBr(cond, body_bb, after_bb);

		// Body block
		builder->SetInsertPoint(body_bb);
		loop_stack.push_back({cond_bb, after_bb});
		emit_stmt(w->body);
		loop_stack.pop_back();

		if (!builder->GetInsertBlock()->hasTerminator()) {
			builder->CreateBr(cond_bb);
		}

		// After block
		builder->SetInsertPoint(after_bb);
		return;
	}

	// 5. Break & Continue
	if (isa<BreakStmt>(stmt)) {
		if (!loop_stack.empty()) {
			builder->CreateBr(loop_stack.back().after_bb);
		}
		return;
	}

	if (isa<ContinueStmt>(stmt)) {
		if (!loop_stack.empty()) {
			builder->CreateBr(loop_stack.back().cond_bb);
		}
		return;
	}

	// 6. Return
	if (isa<ReturnStmt>(stmt)) {
		const auto* r = as<ReturnStmt>(stmt);
		if (r->value) {
			llvm::Value* val = emit_expr(r->value);
			builder->CreateRet(val);
		} else {
			builder->CreateRetVoid();
		}
		return;
	}

	// 7. ExprStmt
	if (isa<ExprStmt>(stmt)) emit_expr(as<ExprStmt>(stmt)->expr);
}





