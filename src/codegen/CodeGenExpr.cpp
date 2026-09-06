module;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

module codegen;

import token;
import ast;
import semantic;
import semantic.symbol;
import semantic.analyzer;

namespace {
	std::string unescape_string(std::string_view raw) {
		if (
			raw.size() >= 2 &&
			raw.front() == '"' &&
			raw.back() == '"'
		)
			raw = raw.substr(1, raw.size() - 2);

		std::string result;
		for (size_t i = 0; i < raw.size(); ++i) {
			if (raw[i] == '\\' && i + 1 < raw.size()) {
				switch (const char next = raw[i + 1]) {
					case 'n': result.push_back('\n');
						break;
					case 't': result.push_back('\t');
						break;
					case 'r': result.push_back('\r');
						break;
					case '\\': result.push_back('\\');
						break;
					case '"': result.push_back('"');
						break;
					case '0': result.push_back('\0');
						break;
					default: result.push_back(next);
						break;
				}
				i++;
			} else result.push_back(raw[i]);
		}
		return result;
	}

	char unescape_char(std::string_view raw) {
		if (
			raw.size() >= 2 &&
			raw.front() == '\'' &&
			raw.back() == '\''
		)
			raw = raw.substr(1, raw.size() - 2);

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
} // anonymous namespace

// ============================================================================
// 1. LValue Evaluation
// ============================================================================

llvm::Value *CodeGen::emit_lvalue(const Expr *expr) {
	if (isa<IdentifierExpr>(expr)) {
		const auto *id = as<IdentifierExpr>(expr);
		const auto name = std::string(id->name);

		if (const auto it = local_vars.find(name); it != local_vars.end()) return it->second;

		std::string const_lookup = name;
		if (analyzer && analyzer->resolved_symbols.contains(expr)) {
			const_lookup = to_llvm_name(analyzer->resolved_symbols.at(expr));
		}
		if (const auto it_g = global_consts.find(const_lookup); it_g != global_consts.end()) return it_g->second;
		if (const auto it_g = global_consts.find(name); it_g != global_consts.end()) return it_g->second;

		return nullptr;
	}

	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);
		auto obj_type = get_sema_type(m->object.get());
		if (
			!obj_type.is_struct() &&
			!(obj_type.is_pointer() &&
			  obj_type.pointee &&
			  obj_type.pointee->is_struct())
		)
			return nullptr;
		const std::string st_name = obj_type.is_struct() ? obj_type.struct_name : obj_type.pointee->struct_name;

		llvm::Value *obj_ptr = nullptr;
		if (obj_type.is_pointer())obj_ptr = emit_expr(m->object.get());
		else obj_ptr = emit_lvalue(m->object.get());

		const auto &sym = analyzer->structs[st_name];
		unsigned field_idx = 0;
		for (size_t i = 0; i < sym.field_order.size(); ++i) {
			if (sym.field_order[i] == m->member) {
				field_idx = static_cast<unsigned>(i);
				break;
			}
		}

		auto *st_ty = struct_types[st_name];
		return builder->CreateStructGEP(st_ty, obj_ptr, field_idx, std::string(m->member));
	}

	if (isa<ArrayLiteralExpr>(expr)) {
		const auto *arr_lit = as<ArrayLiteralExpr>(expr);
		auto sema_ty = get_sema_type(expr);
		auto *arr_type = get_llvm_type(sema_ty);
		auto *fn = builder->GetInsertBlock()->getParent();
		auto *tmp_alloca = create_entry_block_alloca(fn, arr_type, "arr_lit_tmp");
		for (size_t i = 0; i < arr_lit->elements.size(); ++i) {
			llvm::Value *elem_val = emit_expr(arr_lit->elements[i].get());
			llvm::Value *elem_ptr = builder->CreateGEP(
				arr_type, tmp_alloca,
				{builder->getInt32(0), builder->getInt32(static_cast<uint32_t>(i))},
				"init_elem"
			);
			builder->CreateStore(elem_val, elem_ptr);
		}
		return tmp_alloca;
	}

	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		auto target_sema = get_sema_type(idx->target.get());
		auto *index_val = emit_expr(idx->index.get());

		if (target_sema.is_array()) {
			auto *arr_ptr = emit_lvalue(idx->target.get());
			auto *arr_type = get_llvm_type(target_sema);
			return builder->CreateGEP(
				arr_type, arr_ptr,
				{builder->getInt32(0), index_val},
				"arrayidx"
			);
		}

		if (target_sema.is_pointer()) {
			auto *ptr_val = emit_expr(idx->target.get());
			auto *elem_llvm_type = get_llvm_type(*target_sema.pointee);
			return builder->CreateGEP(elem_llvm_type, ptr_val, index_val, "ptridx");
		}

		return nullptr;
	}

	if (
		isa<UnaryExpr>(expr) &&
		as<UnaryExpr>(expr)->op == TokenType::STAR
	)
		return emit_expr(as<UnaryExpr>(expr)->operand.get());

	return nullptr;
}

// ============================================================================
// 2. Expressions
// ============================================================================

llvm::Value *CodeGen::emit_expr(const Expr *expr) {
	if (!expr) return nullptr;

	// 1. Literals
	if (isa<LiteralExpr>(expr)) {
		switch (const auto *lit = as<LiteralExpr>(expr); lit->literal_kind) {
			case LiteralKind::INT: {
				const int64_t val = std::stoll(std::string(lit->raw_text));
				return builder->getInt32(static_cast<int32_t>(val));
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

	// 1b. Array literals: [...]
	if (isa<ArrayLiteralExpr>(expr)) {
		auto *lval = emit_lvalue(expr);
		auto sema_ty = get_sema_type(expr);
		auto *arr_type = get_llvm_type(sema_ty);
		return builder->CreateLoad(arr_type, lval, "arr_val");
	}

	// 2. Identifiers / Variables
	if (isa<IdentifierExpr>(expr)) {
		const auto *id = as<IdentifierExpr>(expr);
		const auto name = std::string(id->name);

		if (auto it = local_vars.find(name); it != local_vars.end()) {
			llvm::AllocaInst * alloca = it->second;
			return builder->CreateLoad(alloca->getAllocatedType(), alloca, name);
		}

		std::string const_lookup = name;
		if (analyzer && analyzer->resolved_symbols.contains(expr)) {
			const_lookup = to_llvm_name(analyzer->resolved_symbols.at(expr));
		}
		if (auto it_g = global_consts.find(const_lookup); it_g != global_consts.end()) {
			llvm::GlobalVariable *gv = it_g->second;
			return builder->CreateLoad(gv->getValueType(), gv, name);
		}
		if (auto it_g = global_consts.find(name); it_g != global_consts.end()) {
			llvm::GlobalVariable *gv = it_g->second;
			return builder->CreateLoad(gv->getValueType(), gv, name);
		}

		return nullptr;
	}

	// 3. Assignment: target = value
	if (isa<AssignExpr>(expr)) {
		const auto *a = as<AssignExpr>(expr);
		auto *lval = emit_lvalue(a->target.get());
		auto *rval = emit_expr(a->value.get());
		builder->CreateStore(rval, lval);
		return rval;
	}

	// 4. Binary Expressions
	if (isa<BinaryExpr>(expr)) {
		const auto *b = as<BinaryExpr>(expr);

		// Short-circuit for &&
		if (b->op == TokenType::AND_AND) {
			llvm::Value *lhs_val = emit_expr(b->left.get());
			llvm::BasicBlock *lhs_bb = builder->GetInsertBlock();
			llvm::Function *fn = lhs_bb->getParent();

			llvm::BasicBlock *rhs_bb = llvm::BasicBlock::Create(*context, "land.rhs", fn);
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "land.merge", fn);

			builder->CreateCondBr(lhs_val, rhs_bb, merge_bb);

			builder->SetInsertPoint(rhs_bb);
			llvm::Value *rhs_val = emit_expr(b->right.get());
			llvm::BasicBlock *rhs_end_bb = builder->GetInsertBlock();
			merge_bb->moveAfter(rhs_end_bb);
			builder->CreateBr(merge_bb);

			builder->SetInsertPoint(merge_bb);
			llvm::PHINode *phi = builder->CreatePHI(builder->getInt1Ty(), 2, "land.res");
			phi->addIncoming(builder->getInt1(false), lhs_bb);
			phi->addIncoming(rhs_val, rhs_end_bb);
			return phi;
		}

		// Short-circuit for ||
		if (b->op == TokenType::OR_OR) {
			llvm::Value *lhs_val = emit_expr(b->left.get());
			llvm::BasicBlock *lhs_bb = builder->GetInsertBlock();
			llvm::Function *fn = lhs_bb->getParent();

			llvm::BasicBlock *rhs_bb = llvm::BasicBlock::Create(*context, "lor.rhs", fn);
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "lor.merge", fn);

			builder->CreateCondBr(lhs_val, merge_bb, rhs_bb);

			builder->SetInsertPoint(rhs_bb);
			llvm::Value *rhs_val = emit_expr(b->right.get());
			llvm::BasicBlock *rhs_end_bb = builder->GetInsertBlock();
			merge_bb->moveAfter(rhs_end_bb);
			builder->CreateBr(merge_bb);

			builder->SetInsertPoint(merge_bb);
			llvm::PHINode *phi = builder->CreatePHI(builder->getInt1Ty(), 2, "lor.res");
			phi->addIncoming(builder->getInt1(true), lhs_bb);
			phi->addIncoming(rhs_val, rhs_end_bb);
			return phi;
		}

		auto *l = emit_expr(b->left.get());
		auto *r = emit_expr(b->right.get());

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

			default: return l;
		}
	}

	// 5. Unary Expressions
	if (isa<UnaryExpr>(expr)) {
		const auto *u = as<UnaryExpr>(expr);
		auto *opnd = emit_expr(u->operand.get());

		switch (u->op) {
			case TokenType::MINUS: return builder->CreateNeg(opnd, "neg");
			case TokenType::BANG: return builder->CreateNot(opnd, "not");
			case TokenType::STAR: {
				auto target_type = get_sema_type(u->operand.get());
				auto elem_type = *target_type.pointee;
				auto *elem_llvm_type = get_llvm_type(elem_type);
				return builder->CreateLoad(elem_llvm_type, opnd, "deref");
			}
			default: return opnd;
		}
	}

	// 6. Function call, struct instantiation or method call: callee(args...)
	if (isa<CallExpr>(expr)) {
		const auto *c = as<CallExpr>(expr);

		// 6a. Direct call or struct instantiation by name
		if (isa<IdentifierExpr>(c->callee.get())) {
			const auto raw_name = std::string(as<IdentifierExpr>(c->callee.get())->name);
			std::string target_name = raw_name;
			if (analyzer && analyzer->resolved_symbols.contains(c)) {
				target_name = to_llvm_name(analyzer->resolved_symbols.at(c));
			}

			// Struct instantiation: Point(10, 20)
			if (struct_types.contains(target_name) || (analyzer && (analyzer->structs.contains(target_name) || (analyzer->resolved_symbols.contains(c) && analyzer->structs.contains(analyzer->resolved_symbols.at(c)))))) {
				llvm::Type* st_type = struct_types[target_name];
				if (!st_type && analyzer && analyzer->resolved_symbols.contains(c)) {
					st_type = struct_types[analyzer->resolved_symbols.at(c)];
				}
				if (!st_type) st_type = struct_types[raw_name];

				llvm::Function* fn = builder->GetInsertBlock()->getParent();
				llvm::AllocaInst* tmp_st = create_entry_block_alloca(fn, st_type, "st_tmp");
				for (size_t i = 0; i < c->args.size(); ++i) {
					llvm::Value* arg_val = emit_expr(c->args[i].get());
					llvm::Value* field_ptr = builder->CreateStructGEP(st_type, tmp_st, static_cast<unsigned>(i), "init_field");
					builder->CreateStore(arg_val, field_ptr);
				}
				return builder->CreateLoad(st_type, tmp_st, "st_val");
			}

			// Regular function call
			auto *callee = module->getFunction(target_name);
			if (!callee && analyzer) {
				std::string fn_lookup = analyzer->resolved_symbols.contains(c) ? analyzer->resolved_symbols.at(c) : target_name;
				auto it = analyzer->functions.find(fn_lookup);
				if (it == analyzer->functions.end()) {
					it = analyzer->functions.find(target_name);
				}
				if (it == analyzer->functions.end()) {
					it = analyzer->functions.find(raw_name);
				}
				if (it != analyzer->functions.end()) {
					std::vector<llvm::Type *> param_types;
					for (const auto &param_type: it->second.param_types) {
						param_types.push_back(get_llvm_type(param_type));
					}
					llvm::Type *ret_type = get_llvm_type(it->second.return_type);
					llvm::FunctionType *fn_type = llvm::FunctionType::get(ret_type, param_types, false);
					callee = llvm::Function::Create(
						fn_type, llvm::Function::ExternalLinkage, target_name, *module
					);
				}
			}

			if (!callee) return nullptr;

			std::vector<llvm::Value *> args;
			for (const auto &arg: c->args) {
				args.push_back(emit_expr(arg.get()));
			}

			return builder->CreateCall(callee, args);
		}

		// 6b. Struct method call: object.method(args...)
		if (isa<MemberExpr>(c->callee.get())) {
			const auto *m = as<MemberExpr>(c->callee.get());
			auto obj_type = get_sema_type(m->object.get());
			std::string st_name = obj_type.is_struct() ? obj_type.struct_name : obj_type.pointee->struct_name;
			std::string mangled = to_llvm_name(st_name) + "_" + std::string(m->member);

			auto *callee = module->getFunction(mangled);
			if (!callee && analyzer) {
				const auto it = analyzer->functions.find(mangled);
				if (it != analyzer->functions.end()) {
					std::vector<llvm::Type *> param_types;
					for (const auto &param_type: it->second.param_types) {
						param_types.push_back(get_llvm_type(param_type));
					}
					llvm::Type *ret_type = get_llvm_type(it->second.return_type);
					llvm::FunctionType *fn_type = llvm::FunctionType::get(ret_type, param_types, false);
					callee = llvm::Function::Create(
						fn_type, llvm::Function::ExternalLinkage, mangled, *module
					);
				}
			}

			if (!callee) return nullptr;

			std::vector<llvm::Value *> args;

			// Load self if required by method
			if (analyzer && analyzer->functions.contains(mangled)) {
				const auto& fn_sym = analyzer->functions.at(mangled);
				if (!fn_sym.param_types.empty() && fn_sym.param_names[0] == "self") {
					const auto& self_expected = fn_sym.param_types[0];
					if (self_expected.is_pointer()) {
						if (obj_type.is_pointer()) {
							args.push_back(emit_expr(m->object.get()));
						} else {
							args.push_back(emit_lvalue(m->object.get()));
						}
					} else {
						args.push_back(emit_expr(m->object.get()));
					}
				}
			}

			// Load remaining arguments
			for (const auto &arg: c->args) {
				args.push_back(emit_expr(arg.get()));
			}

			return builder->CreateCall(callee, args);
		}

		return nullptr;
	}

	// 7. Member access: object.field
	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);

		// 7a. Enum member constant (e.g. Status.OK)
		if (isa<IdentifierExpr>(m->object.get()) && analyzer) {
			const auto id_name = std::string(as<IdentifierExpr>(m->object.get())->name);
			if (auto it_enum = analyzer->enums.find(id_name); it_enum != analyzer->enums.end()) {
				const auto member_name = std::string(m->member);
				if (
					auto it_m = it_enum->second.member_values.find(member_name);
					it_m != it_enum->second.member_values.end()
				) {
					llvm::Type *llvm_ty = get_llvm_type(it_enum->second.underlying_type);
					return llvm::ConstantInt::get(llvm_ty, it_m->second);
				}
			}
		}

		// 7b. .value property on enum variable (e.g. status.value)
		auto obj_sema = get_sema_type(m->object.get());
		if (obj_sema.is_enum() && m->member == "value") return emit_expr(m->object.get());

		// 7c. .len property on array (e.g. arr.len)
		if (obj_sema.is_array() && m->member == "len")
			return builder->getInt32(
				static_cast<int32_t>(obj_sema.array_size)
			);

		llvm::Value *field_ptr = emit_lvalue(m);
		auto field_sema = get_sema_type(m);
		llvm::Type *field_llvm_type = get_llvm_type(field_sema);
		return builder->CreateLoad(field_llvm_type, field_ptr, std::string(m->member));
	}

	// 8. Array indexing: target[index]
	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		llvm::Value *elem_ptr = emit_lvalue(idx);
		auto elem_sema = get_sema_type(idx);
		llvm::Type *elem_llvm_type = get_llvm_type(elem_sema);
		return builder->CreateLoad(elem_llvm_type, elem_ptr);
	}

	// 9. Type cast: expr as TargetType
	if (isa<CastExpr>(expr)) {
		const auto *c = as<CastExpr>(expr);
		auto src_sema = get_sema_type(c->expr.get());
		auto dest_sema = analyzer->resolve_type(c->target_type.get());
		auto *dest_type = get_llvm_type(dest_sema);

		// Array decay: arr as *T
		if (src_sema.is_array() && dest_sema.is_pointer()) {
			if (auto *arr_lval = emit_lvalue(c->expr.get())) {
				auto *arr_ty = get_llvm_type(src_sema);
				return builder->CreateGEP(
					arr_ty, arr_lval,
					{builder->getInt32(0), builder->getInt32(0)},
					"arraydecay"
				);
			}
		}

		llvm::Value *val = emit_expr(c->expr.get());

		const bool src_is_int = src_sema.is_integer() || src_sema.is_enum();
		const bool dest_is_int = dest_sema.is_integer() || dest_sema.is_enum();

		if (src_is_int && dest_is_int) {
			const unsigned src_bits = val->getType()->getIntegerBitWidth();
			const unsigned dest_bits = dest_type->getIntegerBitWidth();
			if (src_bits == dest_bits) return val;
			if (dest_bits > src_bits) {
				bool is_signed = src_sema.is_signed_integer();
				if (src_sema.is_enum() && src_sema.underlying_type)
					is_signed = src_sema.underlying_type->is_signed_integer();
				return is_signed
					       ? builder->CreateSExt(val, dest_type, "sext")
					       : builder->CreateZExt(val, dest_type, "zext");
			}
			return builder->CreateTrunc(val, dest_type, "trunc");
		}

		if (src_sema.is_pointer() && dest_sema.is_pointer()) return val;

		if (src_sema.is_integer() && dest_sema.is_pointer())
			return builder->CreateIntToPtr(
				val, dest_type, "inttoptr"
			);

		if (src_sema.is_pointer() && dest_sema.is_integer())
			return builder->CreatePtrToInt(
				val, dest_type, "ptrtoint"
			);

		if (src_sema.is_char() && dest_sema.is_integer())
			return builder->CreateZExt(
				val, dest_type, "zext_char"
			);

		if (src_sema.is_integer() && dest_sema.is_char())
			return builder->CreateTrunc(
				val, dest_type, "trunc_char"
			);

		return val;
	}

	// 10. Group: (expr)
	if (isa<GroupExpr>(expr)) return emit_expr(as<GroupExpr>(expr)->expr.get());

	return nullptr;
}
