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

void CodeGen::emit_stmt(const Stmt *stmt) {
	if (!stmt) return;

	if (auto *cur_bb = builder->GetInsertBlock(); cur_bb && cur_bb->hasTerminator()) {
		return;
	}

	// 1. Variable declaration: val / var
	if (isa<VarDeclStmt>(stmt)) {
		const auto *v = as<VarDeclStmt>(stmt);
		const auto name = std::string(v->name);
		Semantic sema_ty = v->type_annotation
								 ? analyzer->resolve_type(v->type_annotation)
								 : get_sema_type(v->initializer);

		if (sema_ty && sema_ty->is_array() && sema_ty->array_size == 0 && v->initializer) {
			auto init_sema = get_sema_type(v->initializer);
			if (init_sema && init_sema->is_array()) {
				sema_ty = analyzer->make_array(sema_ty->element_type, init_sema->array_size);
			}
		}
		llvm::Type *var_type = get_llvm_type(sema_ty);

		llvm::Function *fn = builder->GetInsertBlock()->getParent();
		llvm::AllocaInst * alloca = create_entry_block_alloca(fn, var_type, name);
		add_local(v->name, alloca, sema_ty);

		if (v->initializer) {
			if (isa<ArrayLiteralExpr>(v->initializer)) {
				const auto *arr_lit = as<ArrayLiteralExpr>(v->initializer);
				for (size_t i = 0; i < arr_lit->elements.size(); ++i) {
					llvm::Value *elem_val = emit_expr(arr_lit->elements[i]);
					llvm::Value *elem_ptr = builder->CreateGEP(
						var_type, alloca,
						{builder->getInt32(0), builder->getInt32(static_cast<int32_t>(i))},
						name + "_init"
					);
					builder->CreateStore(elem_val, elem_ptr);
				}
			} else if (
				isa<CallExpr>(v->initializer) &&
				isa<IdentifierExpr>(as<CallExpr>(v->initializer)->callee) &&
				analyzer && (
					analyzer->structs.contains(as<IdentifierExpr>(as<CallExpr>(v->initializer)->callee)->name) ||
					(analyzer->resolved_symbols.contains(as<CallExpr>(v->initializer)) &&
					 analyzer->structs.contains(analyzer->resolved_symbols.at(as<CallExpr>(v->initializer))))
				)
			) {
				const auto *call = as<CallExpr>(v->initializer);
				for (size_t i = 0; i < call->args.size(); ++i) {
					llvm::Value *arg_val = emit_expr(call->args[i]);
					llvm::Value *field_ptr = builder->CreateStructGEP(
						var_type, alloca, static_cast<unsigned>(i), name + "_f"
					);
					builder->CreateStore(arg_val, field_ptr);
				}
			} else {
				llvm::Value *init_val = emit_expr(v->initializer);
				if (var_type->isPointerTy() && init_val->getType()->isStructTy()) {
					init_val = builder->CreateExtractValue(init_val, 0, "str_ptr");
				}
				builder->CreateStore(init_val, alloca);
			}
		}
		return;
	}

	// 2. Block: { ... }
	if (isa<BlockStmt>(stmt)) {
		const auto *b = as<BlockStmt>(stmt);
		push_scope();
		for (const auto &s: b->statements) {
			emit_stmt(s);
		}
		pop_scope();
		return;
	}

	// 3. If statement: if (cond) { ... } else { ... }
	if (isa<IfStmt>(stmt)) {
		const auto *i = as<IfStmt>(stmt);
		llvm::Value *cond = emit_expr(i->condition);
		llvm::Function *fn = builder->GetInsertBlock()->getParent();

		llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(*context, "then", fn);
		llvm::BasicBlock *else_bb = i->else_branch ? llvm::BasicBlock::Create(*context, "else") : nullptr;
		llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "if_merge");

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
		const auto *w = as<WhileStmt>(stmt);
		llvm::Function *fn = builder->GetInsertBlock()->getParent();

		llvm::BasicBlock *cond_bb = llvm::BasicBlock::Create(*context, "while_cond", fn);
		llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(*context, "while_body", fn);
		llvm::BasicBlock *after_bb = llvm::BasicBlock::Create(*context, "while_after", fn);

		builder->CreateBr(cond_bb);

		// Cond block
		builder->SetInsertPoint(cond_bb);
		llvm::Value *cond = emit_expr(w->condition);
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
		const auto *r = as<ReturnStmt>(stmt);
		if (r->value) {
			llvm::Value *val = emit_expr(r->value);
			if (val && val->getType()->isStructTy() &&
				builder->GetInsertBlock()->getParent()->getReturnType()->isPointerTy()) {
				val = builder->CreateExtractValue(val, 0, "str_ptr");
			}
			builder->CreateRet(val);
		} else {
			builder->CreateRetVoid();
		}
		return;
	}

	// 7. ExprStmt
	if (isa<ExprStmt>(stmt)) {
		emit_expr(as<ExprStmt>(stmt)->expr);
		return;
	}

	// 8. When statement
	if (isa<WhenStmt>(stmt)) emit_when_stmt(as<WhenStmt>(stmt));
}

llvm::Value *CodeGen::emit_equality(llvm::Value *l, llvm::Value *r, Semantic sema_ty) {
	if (sema_ty && sema_ty->is_str()) {
		auto *a_len = builder->CreateExtractValue(l, 1, "a.len");
		auto *b_len = builder->CreateExtractValue(r, 1, "b.len");
		auto *len_eq = builder->CreateICmpEQ(a_len, b_len, "len.eq");

		auto *a_data = builder->CreateExtractValue(l, 0, "a.data");
		auto *b_data = builder->CreateExtractValue(r, 0, "b.data");
		auto *memcmp_fn = module->getFunction("memcmp");
		auto *cmp_result = builder->CreateCall(memcmp_fn, {a_data, b_data, a_len}, "memcmp");
		auto *content_eq = builder->CreateICmpEQ(cmp_result, builder->getInt32(0), "content.eq");

		return builder->CreateAnd(len_eq, content_eq, "str.eq");
	}

	if (l->getType()->isIntegerTy() && r->getType()->isIntegerTy()) {
		if (l->getType() != r->getType()) {
			r = builder->CreateIntCast(r, l->getType(), sema_ty ? sema_ty->is_signed_integer() : true);
		}
	}
	return builder->CreateICmpEQ(l, r, "eq");
}

void CodeGen::emit_when_stmt(const WhenStmt *stmt) {
	if (!stmt || stmt->arms.empty()) return;

	llvm::Function *fn = builder->GetInsertBlock()->getParent();
	llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "when_merge");

	llvm::Value *cond_val = nullptr;
	Semantic cond_sema = nullptr;
	if (stmt->condition) {
		cond_val = emit_expr(stmt->condition);
		cond_sema = get_sema_type(stmt->condition);
	}

	for (size_t i = 0; i < stmt->arms.size(); ++i) {
		const auto &arm = stmt->arms[i];
		llvm::BasicBlock *arm_body_bb = llvm::BasicBlock::Create(*context, "when_arm_body", fn);

		if (arm.is_else) {
			builder->CreateBr(arm_body_bb);
			builder->SetInsertPoint(arm_body_bb);
			emit_stmt(arm.body);
			if (!builder->GetInsertBlock()->hasTerminator()) {
				builder->CreateBr(merge_bb);
			}
			break;
		}

		llvm::BasicBlock *next_arm_bb = (i + 1 < stmt->arms.size())
											? llvm::BasicBlock::Create(*context, "when_arm_next", fn)
											: merge_bb;

		for (size_t j = 0; j < arm.patterns.size(); ++j) {
			llvm::BasicBlock *next_pat_bb = (j + 1 < arm.patterns.size())
												? llvm::BasicBlock::Create(*context, "when_pat_next", fn)
												: next_arm_bb;

			llvm::Value *pat_val = emit_expr(arm.patterns[j]);
			llvm::Value *match_cond = nullptr;
			if (cond_val) {
				match_cond = emit_equality(cond_val, pat_val, cond_sema);
			} else {
				match_cond = pat_val;
			}
			builder->CreateCondBr(match_cond, arm_body_bb, next_pat_bb);
			if (j + 1 < arm.patterns.size()) {
				builder->SetInsertPoint(next_pat_bb);
			}
		}

		builder->SetInsertPoint(arm_body_bb);
		emit_stmt(arm.body);
		if (!builder->GetInsertBlock()->hasTerminator()) {
			builder->CreateBr(merge_bb);
		}

		if (i + 1 < stmt->arms.size()) {
			builder->SetInsertPoint(next_arm_bb);
		}
	}

	fn->insert(fn->end(), merge_bb);
	builder->SetInsertPoint(merge_bb);
}
