module;

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/TargetParser/Host.h>

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

	explicit CodeGen(Analyzer* sema, const std::string_view module_name = "kobel_module")
		: context(std::make_unique<llvm::LLVMContext>()),
		  module(std::make_unique<llvm::Module>(std::string(module_name), *context)),
		  builder(std::make_unique<llvm::IRBuilder<>>(*context)),
		  analyzer(sema) {
		module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));
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
		llvm::IRBuilder<> tmp_builder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
		return tmp_builder.CreateAlloca(type, nullptr, std::string(name));
	}

	Semantic get_sema_type(const Expr* expr) {
		if (!expr) return Semantic::make_error();
		if (analyzer) {
			auto ty = analyzer->get_expr_type(expr);
			if (!ty.is_error()) return ty;
		}

		if (isa<IdentifierExpr>(expr)) {
			const std::string name = std::string(as<IdentifierExpr>(expr)->name);
			auto it = local_types.find(name);
			if (it != local_types.end()) return it->second;
			if (analyzer) {
				auto it_c = analyzer->constants.find(name);
				if (it_c != analyzer->constants.end()) return it_c->second.type;
			}
		}
		return Semantic::make_error();
	}

	// ========================================================================
	// 2. Tiện ích Chuỗi & Ký tự
	// ========================================================================

	static std::string unescape_string(std::string_view raw) {
		if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
			raw = raw.substr(1, raw.size() - 2);
		}
		std::string result;
		for (size_t i = 0; i < raw.size(); ++i) {
			if (raw[i] == '\\' && i + 1 < raw.size()) {
				const char next = raw[i + 1];
				switch (next) {
					case 'n': result.push_back('\n'); break;
					case 't': result.push_back('\t'); break;
					case 'r': result.push_back('\r'); break;
					case '\\': result.push_back('\\'); break;
					case '"': result.push_back('"'); break;
					case '0': result.push_back('\0'); break;
					default: result.push_back(next); break;
				}
				i++;
			} else {
				result.push_back(raw[i]);
			}
		}
		return result;
	}

	static char unescape_char(std::string_view raw) {
		if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') {
			raw = raw.substr(1, raw.size() - 2);
		}
		if (raw.empty()) return '\0';
		if (raw.size() >= 2 && raw[0] == '\\') {
			switch (raw[1]) {
				case 'n': return '\n';
				case 't': return '\t';
				case 'r': return '\r';
				case '\\': return '\\';
				case '\'': return '\'';
				case '0': return '\0';
				default: return raw[1];
			}
		}
		return raw[0];
	}

	// ========================================================================
	// 3. LValue Evaluation (Địa chỉ gán)
	// ========================================================================

	llvm::Value* emit_lvalue(const Expr* expr) {
		if (isa<IdentifierExpr>(expr)) {
			const auto* id = as<IdentifierExpr>(expr);
			const std::string name = std::string(id->name);

			auto it = local_vars.find(name);
			if (it != local_vars.end()) return it->second;

			auto it_g = global_consts.find(name);
			if (it_g != global_consts.end()) return it_g->second;

			return nullptr;
		}

		if (isa<MemberExpr>(expr)) {
			const auto* m = as<MemberExpr>(expr);
			auto obj_type = get_sema_type(m->object.get());
			const std::string st_name = obj_type.is_struct() ? obj_type.struct_name : obj_type.pointee->struct_name;

			llvm::Value* obj_ptr = nullptr;
			if (obj_type.is_pointer()) {
				obj_ptr = emit_expr(m->object.get());
			} else {
				obj_ptr = emit_lvalue(m->object.get());
			}

			const auto& sym = analyzer->structs[st_name];
			unsigned field_idx = 0;
			for (size_t i = 0; i < sym.field_order.size(); ++i) {
				if (sym.field_order[i] == m->member) {
					field_idx = static_cast<unsigned>(i);
					break;
				}
			}

			llvm::StructType* st_ty = struct_types[st_name];
			return builder->CreateStructGEP(st_ty, obj_ptr, field_idx, std::string(m->member));
		}

		if (isa<IndexExpr>(expr)) {
			const auto* idx = as<IndexExpr>(expr);
			auto target_type = analyzer->analyze_expr(idx->target.get());
			auto elem_type = *target_type.pointee;
			llvm::Type* elem_llvm_type = get_llvm_type(elem_type);

			llvm::Value* target_ptr = emit_expr(idx->target.get());
			llvm::Value* index_val = emit_expr(idx->index.get());

			return builder->CreateGEP(elem_llvm_type, target_ptr, index_val);
		}

		if (isa<UnaryExpr>(expr)) {
			const auto* u = as<UnaryExpr>(expr);
			if (u->op == TokenType::STAR) {
				// *ptr -> lvalue chính là con trỏ
				return emit_expr(u->operand.get());
			}
		}

		return nullptr;
	}

	// ========================================================================
	// 4. Sinh mã Biểu thức (Expressions)
	// ========================================================================

	llvm::Value* emit_expr(const Expr* expr) {
		if (!expr) return nullptr;

		// 1. Literal
		if (isa<LiteralExpr>(expr)) {
			const auto* lit = as<LiteralExpr>(expr);
			switch (lit->literal_kind) {
				case LiteralKind::INT: {
					const int64_t val = std::stoll(std::string(lit->raw_text));
					return builder->getInt32(static_cast<uint32_t>(val));
				}
				case LiteralKind::BOOL:
					return builder->getInt1(lit->raw_text == "true");

				case LiteralKind::CHAR:
					return builder->getInt8(static_cast<uint8_t>(unescape_char(lit->raw_text)));

				case LiteralKind::STRING: {
					const std::string s = unescape_string(lit->raw_text);
					return builder->CreateGlobalString(s, ".str", 0, module.get());
				}

				case LiteralKind::NULL_VAL:
					return llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));

				default:
					return builder->getInt32(0);
			}
		}

		// 2. Identifier
		if (isa<IdentifierExpr>(expr)) {
			const auto* id = as<IdentifierExpr>(expr);
			const std::string name = std::string(id->name);

			auto it = local_vars.find(name);
			if (it != local_vars.end()) {
				llvm::AllocaInst* alloca = it->second;
				return builder->CreateLoad(alloca->getAllocatedType(), alloca, name);
			}

			auto it_g = global_consts.find(name);
			if (it_g != global_consts.end()) {
				llvm::GlobalVariable* gv = it_g->second;
				return builder->CreateLoad(gv->getValueType(), gv, name);
			}

			return nullptr;
		}

		// 3. Phép gán: target = value
		if (isa<AssignExpr>(expr)) {
			const auto* a = as<AssignExpr>(expr);
			llvm::Value* lval = emit_lvalue(a->target.get());
			llvm::Value* rval = emit_expr(a->value.get());
			builder->CreateStore(rval, lval);
			return rval;
		}

		// 4. Biểu thức Nhị phân
		if (isa<BinaryExpr>(expr)) {
			const auto* b = as<BinaryExpr>(expr);
			llvm::Value* l = emit_expr(b->left.get());
			llvm::Value* r = emit_expr(b->right.get());

			auto left_sema = get_sema_type(b->left.get());
			const bool is_unsigned = !left_sema.is_signed_integer();

			switch (b->op) {
				case TokenType::PLUS: return builder->CreateAdd(l, r, "add");
				case TokenType::MINUS: return builder->CreateSub(l, r, "sub");
				case TokenType::STAR: return builder->CreateMul(l, r, "mul");
				case TokenType::SLASH:
					return is_unsigned ? builder->CreateUDiv(l, r, "udiv") : builder->CreateSDiv(l, r, "sdiv");
				case TokenType::PERCENT:
					return is_unsigned ? builder->CreateURem(l, r, "urem") : builder->CreateSRem(l, r, "srem");

				case TokenType::EQUAL_EQUAL: return builder->CreateICmpEQ(l, r, "eq");
				case TokenType::BANG_EQUAL: return builder->CreateICmpNE(l, r, "ne");
				case TokenType::LESS:
					return is_unsigned ? builder->CreateICmpULT(l, r, "ult") : builder->CreateICmpSLT(l, r, "slt");
				case TokenType::LESS_EQUAL:
					return is_unsigned ? builder->CreateICmpULE(l, r, "ule") : builder->CreateICmpSLE(l, r, "sle");
				case TokenType::GREATER:
					return is_unsigned ? builder->CreateICmpUGT(l, r, "ugt") : builder->CreateICmpSGT(l, r, "sgt");
				case TokenType::GREATER_EQUAL:
					return is_unsigned ? builder->CreateICmpUGE(l, r, "uge") : builder->CreateICmpSGE(l, r, "sge");

				case TokenType::AND_AND: return builder->CreateAnd(l, r, "land");
				case TokenType::OR_OR: return builder->CreateOr(l, r, "lor");

				default: return l;
			}
		}

		// 5. Biểu thức Một ngôi
		if (isa<UnaryExpr>(expr)) {
			const auto* u = as<UnaryExpr>(expr);
			llvm::Value* opnd = emit_expr(u->operand.get());

			switch (u->op) {
				case TokenType::MINUS: return builder->CreateNeg(opnd, "neg");
				case TokenType::BANG: return builder->CreateNot(opnd, "not");
				case TokenType::STAR: {
					auto target_type = get_sema_type(u->operand.get());
					auto elem_type = *target_type.pointee;
					llvm::Type* elem_llvm_type = get_llvm_type(elem_type);
					return builder->CreateLoad(elem_llvm_type, opnd, "deref");
				}
				default: return opnd;
			}
		}

		// 6. Lệnh gọi hàm: callee(args...)
		if (isa<CallExpr>(expr)) {
			const auto* c = as<CallExpr>(expr);
			const auto fn_name = as<IdentifierExpr>(c->callee.get())->name;
			llvm::Function* callee = module->getFunction(std::string(fn_name));

			std::vector<llvm::Value*> args;
			for (const auto& arg : c->args) {
				args.push_back(emit_expr(arg.get()));
			}

			return builder->CreateCall(callee, args);
		}

		// 7. Truy cập trường struct: object.field
		if (isa<MemberExpr>(expr)) {
			const auto* m = as<MemberExpr>(expr);
			llvm::Value* field_ptr = emit_lvalue(m);
			auto field_sema = get_sema_type(m);
			llvm::Type* field_llvm_type = get_llvm_type(field_sema);
			return builder->CreateLoad(field_llvm_type, field_ptr, std::string(m->member));
		}

		// 8. Chỉ mục mảng: target[index]
		if (isa<IndexExpr>(expr)) {
			const auto* idx = as<IndexExpr>(expr);
			llvm::Value* elem_ptr = emit_lvalue(idx);
			auto elem_sema = get_sema_type(idx);
			llvm::Type* elem_llvm_type = get_llvm_type(elem_sema);
			return builder->CreateLoad(elem_llvm_type, elem_ptr);
		}

		// 9. Ép kiểu: expr as TargetType
		if (isa<CastExpr>(expr)) {
			const auto* c = as<CastExpr>(expr);
			llvm::Value* val = emit_expr(c->expr.get());
			auto src_sema = get_sema_type(c->expr.get());
			auto dest_sema = analyzer->resolve_type(c->target_type.get());
			llvm::Type* dest_type = get_llvm_type(dest_sema);

			if (src_sema.is_integer() && dest_sema.is_integer()) {
				const unsigned src_bits = val->getType()->getIntegerBitWidth();
				const unsigned dest_bits = dest_type->getIntegerBitWidth();
				if (src_bits == dest_bits) return val;
				if (dest_bits > src_bits) {
					return src_sema.is_signed_integer() ? builder->CreateSExt(val, dest_type, "sext")
					                                     : builder->CreateZExt(val, dest_type, "zext");
				}
				return builder->CreateTrunc(val, dest_type, "trunc");
			}

			if (src_sema.is_pointer() && dest_sema.is_pointer()) {
				return val;
			}

			if (src_sema.is_integer() && dest_sema.is_pointer()) {
				return builder->CreateIntToPtr(val, dest_type, "inttoptr");
			}

			if (src_sema.is_pointer() && dest_sema.is_integer()) {
				return builder->CreatePtrToInt(val, dest_type, "ptrtoint");
			}

			if (src_sema.is_char() && dest_sema.is_integer()) {
				return builder->CreateZExt(val, dest_type, "zext_char");
			}

			if (src_sema.is_integer() && dest_sema.is_char()) {
				return builder->CreateTrunc(val, dest_type, "trunc_char");
			}

			return val;
		}

		// 10. Group: (expr)
		if (isa<GroupExpr>(expr)) {
			return emit_expr(as<GroupExpr>(expr)->expr.get());
		}

		return nullptr;
	}

	// ========================================================================
	// 5. Sinh mã Câu lệnh (Statements)
	// ========================================================================

	void emit_stmt(const Stmt* stmt) {
		if (auto* cur_bb = builder->GetInsertBlock(); cur_bb && cur_bb->hasTerminator()) {
			return;
		}

		// 1. Khai báo biến: val / var
		if (isa<VarDeclStmt>(stmt)) {
			const auto* v = as<VarDeclStmt>(stmt);
			const std::string name = std::string(v->name);
			auto sema_ty = analyzer->resolve_type(v->type_annotation.get());
			llvm::Type* var_type = get_llvm_type(sema_ty);

			llvm::Function* fn = builder->GetInsertBlock()->getParent();
			llvm::AllocaInst* alloca = create_entry_block_alloca(fn, var_type, name);
			local_vars[name] = alloca;
			local_types[name] = sema_ty;

			if (v->initializer) {
				llvm::Value* init_val = emit_expr(v->initializer.get());
				builder->CreateStore(init_val, alloca);
			}
			return;
		}

		// 2. Khối lệnh: { ... }
		if (isa<BlockStmt>(stmt)) {
			const auto* b = as<BlockStmt>(stmt);
			for (const auto& s : b->statements) {
				emit_stmt(s.get());
			}
			return;
		}

		// 3. Câu lệnh if: if (cond) { ... } else { ... }
		if (isa<IfStmt>(stmt)) {
			const auto* i = as<IfStmt>(stmt);
			llvm::Value* cond = emit_expr(i->condition.get());
			llvm::Function* fn = builder->GetInsertBlock()->getParent();

			llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(*context, "then", fn);
			llvm::BasicBlock* else_bb = i->else_branch ? llvm::BasicBlock::Create(*context, "else") : nullptr;
			llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(*context, "if_merge");

			builder->CreateCondBr(cond, then_bb, i->else_branch ? else_bb : merge_bb);

			// Then block
			builder->SetInsertPoint(then_bb);
			emit_stmt(i->then_branch.get());
			if (!builder->GetInsertBlock()->hasTerminator()) {
				builder->CreateBr(merge_bb);
			}

			// Else block
			if (i->else_branch) {
				fn->insert(fn->end(), else_bb);
				builder->SetInsertPoint(else_bb);
				emit_stmt(i->else_branch.get());
				if (!builder->GetInsertBlock()->hasTerminator()) {
					builder->CreateBr(merge_bb);
				}
			}

			// Merge block
			fn->insert(fn->end(), merge_bb);
			builder->SetInsertPoint(merge_bb);
			return;
		}

		// 4. Vòng lặp while: while (cond) { ... }
		if (isa<WhileStmt>(stmt)) {
			const auto* w = as<WhileStmt>(stmt);
			llvm::Function* fn = builder->GetInsertBlock()->getParent();

			llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(*context, "while_cond", fn);
			llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(*context, "while_body", fn);
			llvm::BasicBlock* after_bb = llvm::BasicBlock::Create(*context, "while_after", fn);

			builder->CreateBr(cond_bb);

			// Cond block
			builder->SetInsertPoint(cond_bb);
			llvm::Value* cond = emit_expr(w->condition.get());
			builder->CreateCondBr(cond, body_bb, after_bb);

			// Body block
			builder->SetInsertPoint(body_bb);
			loop_stack.push_back({cond_bb, after_bb});
			emit_stmt(w->body.get());
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
				llvm::Value* val = emit_expr(r->value.get());
				builder->CreateRet(val);
			} else {
				builder->CreateRetVoid();
			}
			return;
		}

		// 7. ExprStmt
		if (isa<ExprStmt>(stmt)) {
			emit_expr(as<ExprStmt>(stmt)->expr.get());
			return;
		}
	}

	// ========================================================================
	// 6. Sinh mã Khai báo Cấp cao (Declarations)
	// ========================================================================

	void emit_struct_decl(const StructDecl* st) {
		const std::string name = std::string(st->name);
		std::vector<llvm::Type*> field_types;

		for (const auto& field : st->fields) {
			auto sema_ty = analyzer->resolve_type(field.type.get());
			field_types.push_back(get_llvm_type(sema_ty));
		}

		llvm::StructType* struct_ty = llvm::StructType::create(*context, field_types, name);
		struct_types[name] = struct_ty;
	}

	void emit_const_decl(const ConstDecl* c) {
		const std::string name = std::string(c->name);
		auto sema_ty = analyzer->resolve_type(c->type.get());
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

	void emit_fn_decl(const FnDecl* fn_decl) {
		const std::string name = std::string(fn_decl->name);
		llvm::Function* fn = module->getFunction(name);

		if (!fn) {
			std::vector<llvm::Type*> param_types;
			for (const auto& p : fn_decl->params) {
				auto sema_ty = analyzer->resolve_type(p.type.get());
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
			const std::string param_name = std::string(fn_decl->params[idx].name);
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

	void emit_extern_block(const ExternBlock* ext) {
		for (const auto& fn : ext->declarations) {
			emit_fn_decl(fn.get());
		}
	}

	// ========================================================================
	// 7. Tổng thể Sinh mã & Kiểm định Module
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

		// 4. Functions
		for (const auto& decl : program->declarations) {
			if (isa<FnDecl>(decl.get())) {
				emit_fn_decl(as<FnDecl>(decl.get()));
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
