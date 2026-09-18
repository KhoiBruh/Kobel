module;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <charconv>
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
	if (analyzer) {
		if (auto it = analyzer->resolved_symbols.find(expr); it != analyzer->resolved_symbols.end()) {
			std::string const_lookup = to_llvm_name(it->second);
			if (const auto it_g = global_consts.find(const_lookup); it_g != global_consts.end()) return it_g->second;
		}
	}

	if (isa<IdentifierExpr>(expr)) {
		const auto *id = as<IdentifierExpr>(expr);
		const auto name = std::string(id->name);

		if (auto *alloca = lookup_local_var(name)) return alloca;

		std::string const_lookup = name;
		if (analyzer) {
			if (auto it = analyzer->resolved_symbols.find(expr); it != analyzer->resolved_symbols.end()) {
				const_lookup = to_llvm_name(it->second);
			}
		}
		if (const auto it_g = global_consts.find(const_lookup); it_g != global_consts.end()) return it_g->second;
		if (const auto it_g = global_consts.find(name); it_g != global_consts.end()) return it_g->second;

		return nullptr;
	}

	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);
		auto obj_type = get_sema_type(m->object);
		if (
			!obj_type->is_struct() &&
			!(obj_type->is_pointer() &&
			  obj_type->pointee &&
			  obj_type->pointee->is_struct())
		)
			return nullptr;
		const std::string st_name = obj_type->is_struct() ? obj_type->struct_name : obj_type->pointee->struct_name;

		llvm::Value *obj_ptr = nullptr;
		if (obj_type->is_pointer())obj_ptr = emit_expr(m->object);
		else obj_ptr = emit_lvalue(m->object);

		if (!analyzer) return nullptr;
		const StructSymbol *sym_ptr = nullptr;
		if (auto it = analyzer->structs.find(st_name); it != analyzer->structs.end()) {
			sym_ptr = &it->second;
		} else if (auto it = analyzer->structs.find(to_llvm_name(st_name)); it != analyzer->structs.end()) {
			sym_ptr = &it->second;
		}
		if (!sym_ptr) return nullptr;

		const auto &sym = *sym_ptr;
		unsigned field_idx = 0;
		bool found_field = false;
		for (size_t i = 0; i < sym.field_order.size(); ++i) {
			if (sym.field_order[i] == m->member) {
				field_idx = static_cast<unsigned>(i);
				found_field = true;
				break;
			}
		}
		if (!found_field) return nullptr;

		llvm::StructType *st_ty = nullptr;
		if (auto it = struct_types.find(st_name); it != struct_types.end()) st_ty = it->second;
		if (!st_ty) {
			if (auto it = struct_types.find(to_llvm_name(st_name)); it != struct_types.end()) st_ty = it->second;
		}
		if (!st_ty) {
			std::string lookup_name = analyzer->structs.contains(st_name) ? st_name :
			                          (analyzer->structs.contains(to_llvm_name(st_name)) ? to_llvm_name(st_name) : "");
			if (!lookup_name.empty()) {
				emit_instantiated_struct(lookup_name);
				if (auto it = struct_types.find(to_llvm_name(lookup_name)); it != struct_types.end()) st_ty = it->second;
				if (!st_ty) {
					if (auto it = struct_types.find(lookup_name); it != struct_types.end()) st_ty = it->second;
				}
			}
		}
		if (!st_ty || !obj_ptr) return nullptr;
		return builder->CreateStructGEP(st_ty, obj_ptr, field_idx, std::string(m->member));
	}

	if (isa<ArrayLiteralExpr>(expr)) {
		const auto *arr_lit = as<ArrayLiteralExpr>(expr);
		auto sema_ty = get_sema_type(expr);
		auto *arr_type = get_llvm_type(sema_ty);
		auto *fn = builder->GetInsertBlock()->getParent();
		auto *tmp_alloca = create_entry_block_alloca(fn, arr_type, "arr_lit_tmp");
		for (size_t i = 0; i < arr_lit->elements.size(); ++i) {
			llvm::Value *elem_val = emit_expr(arr_lit->elements[i]);
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
		auto target_sema = get_sema_type(idx->target);
		auto *index_val = emit_expr(idx->index);

		if (target_sema->is_array()) {
			auto *arr_ptr = emit_lvalue(idx->target);
			auto *arr_type = get_llvm_type(target_sema);
			return builder->CreateGEP(
				arr_type, arr_ptr,
				{builder->getInt32(0), index_val},
				"arrayidx"
			);
		}

		if (target_sema->is_pointer()) {
			auto *ptr_val = emit_expr(idx->target);
			auto *elem_llvm_type = get_llvm_type(target_sema->pointee);
			return builder->CreateGEP(elem_llvm_type, ptr_val, index_val, "ptridx");
		}

		// str[index]: extract data ptr, GEP to char
		if (target_sema->is_str()) {
			auto *str_val = emit_expr(idx->target);
			auto *data_ptr = builder->CreateExtractValue(str_val, 0, "str.data");
			return builder->CreateGEP(builder->getInt8Ty(), data_ptr, index_val, "str.idx");
		}

		return nullptr;
	}

	if (
		isa<UnaryExpr>(expr) &&
		as<UnaryExpr>(expr)->op == TokenType::STAR
	)
		return emit_expr(as<UnaryExpr>(expr)->operand);

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
				const int64_t val = parse_kobel_int(lit->raw_text);
				auto sema_ty = get_sema_type(expr);
				switch (sema_ty->kind) {
					case SemaType::I8:
					case SemaType::U8:
						return builder->getInt8(static_cast<uint8_t>(val));
					case SemaType::I16:
					case SemaType::U16:
						return builder->getInt16(static_cast<uint16_t>(val));
					case SemaType::I64:
					case SemaType::U64:
					case SemaType::ISZ:
					case SemaType::USZ:
						return builder->getInt64(static_cast<uint64_t>(val));
					default:
						return builder->getInt32(static_cast<int32_t>(val));
				}
			}

			case LiteralKind::FLOAT: {
				std::string s(lit->raw_text);
				while (!s.empty() && (s.back() == 'F' || s.back() == 'D' || s.back() == '_')) s.pop_back();
				double val = 0.0;
				std::from_chars(s.data(), s.data() + s.size(), val);
				auto sema_ty = get_sema_type(expr);
				if (sema_ty && sema_ty->kind == SemaType::F32) {
					return llvm::ConstantFP::get(*context, llvm::APFloat(static_cast<float>(val)));
				}
				return llvm::ConstantFP::get(*context, llvm::APFloat(val));
			}

			case LiteralKind::BOOL:
				return builder->getInt1(lit->raw_text == "true");

			case LiteralKind::CHAR:
				return builder->getInt8(static_cast<uint8_t>(unescape_char(lit->raw_text)));

			case LiteralKind::STRING: {
				const std::string s = unescape_string(lit->raw_text);
				// CreateGlobalString appends \0 automatically
				auto *global = builder->CreateGlobalString(s, ".str", 0, module.get());
				// Wrap as str struct: { data, len, cap=0 (STATIC) }
				auto *str_ty = struct_types["str"];
				llvm::Value *str_val = llvm::UndefValue::get(str_ty);
				str_val = builder->CreateInsertValue(str_val, global, 0); // data
				str_val = builder->CreateInsertValue(str_val, builder->getInt64(s.size()), 1); // len
				str_val = builder->CreateInsertValue(str_val, builder->getInt64(0), 2); // cap = 0 (STATIC)
				return str_val;
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

		if (auto *alloca = lookup_local_var(name)) {
			return builder->CreateLoad(alloca->getAllocatedType(), alloca, name);
		}

		std::string const_lookup = name;
		if (analyzer) {
			if (auto it = analyzer->resolved_symbols.find(expr); it != analyzer->resolved_symbols.end()) {
				const_lookup = to_llvm_name(it->second);
			}
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
		auto *lval = emit_lvalue(a->target);
		auto *rval = emit_expr(a->value);
		auto target_sema = get_sema_type(a->target);
		if (target_sema->is_pointer() && rval->getType()->isStructTy()) {
			rval = builder->CreateExtractValue(rval, 0, "str_ptr");
		}
		builder->CreateStore(rval, lval);
		return rval;
	}

	// 4. Binary Expressions
	if (isa<BinaryExpr>(expr)) {
		const auto *b = as<BinaryExpr>(expr);

		// Short-circuit for &&
		if (b->op == TokenType::AND_AND) {
			llvm::Value *lhs_val = emit_expr(b->left);
			llvm::BasicBlock *lhs_bb = builder->GetInsertBlock();
			llvm::Function *fn = lhs_bb->getParent();

			llvm::BasicBlock *rhs_bb = llvm::BasicBlock::Create(*context, "land.rhs", fn);
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "land.merge", fn);

			builder->CreateCondBr(lhs_val, rhs_bb, merge_bb);

			builder->SetInsertPoint(rhs_bb);
			llvm::Value *rhs_val = emit_expr(b->right);
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
			llvm::Value *lhs_val = emit_expr(b->left);
			llvm::BasicBlock *lhs_bb = builder->GetInsertBlock();
			llvm::Function *fn = lhs_bb->getParent();

			llvm::BasicBlock *rhs_bb = llvm::BasicBlock::Create(*context, "lor.rhs", fn);
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "lor.merge", fn);

			builder->CreateCondBr(lhs_val, merge_bb, rhs_bb);

			builder->SetInsertPoint(rhs_bb);
			llvm::Value *rhs_val = emit_expr(b->right);
			llvm::BasicBlock *rhs_end_bb = builder->GetInsertBlock();
			merge_bb->moveAfter(rhs_end_bb);
			builder->CreateBr(merge_bb);

			builder->SetInsertPoint(merge_bb);
			llvm::PHINode *phi = builder->CreatePHI(builder->getInt1Ty(), 2, "lor.res");
			phi->addIncoming(builder->getInt1(true), lhs_bb);
			phi->addIncoming(rhs_val, rhs_end_bb);
			return phi;
		}

		auto *l = emit_expr(b->left);
		auto *r = emit_expr(b->right);

		auto left_sema = get_sema_type(b->left);

		// str binary ops
		if (left_sema->is_str()) {
			auto *str_ty = struct_types["str"];

			if (b->op == TokenType::PLUS) {
				// Alloca + store both operands, pass by pointer to __kobel_str_concat
				auto *fn = builder->GetInsertBlock()->getParent();
				auto *l_alloca = create_entry_block_alloca(fn, str_ty, "concat.l");
				auto *r_alloca = create_entry_block_alloca(fn, str_ty, "concat.r");
				builder->CreateStore(l, l_alloca);
				builder->CreateStore(r, r_alloca);
				auto *concat_fn = module->getFunction("__kobel_str_concat");
				return builder->CreateCall(concat_fn, {l_alloca, r_alloca}, "concat");
			}

			if (b->op == TokenType::EQUAL_EQUAL || b->op == TokenType::BANG_EQUAL) {
				// Compare len first, then memcmp
				auto *a_len = builder->CreateExtractValue(l, 1, "a.len");
				auto *b_len = builder->CreateExtractValue(r, 1, "b.len");
				auto *len_eq = builder->CreateICmpEQ(a_len, b_len, "len.eq");

				auto *a_data = builder->CreateExtractValue(l, 0, "a.data");
				auto *b_data = builder->CreateExtractValue(r, 0, "b.data");
				auto *memcmp_fn = module->getFunction("memcmp");
				auto *cmp_result = builder->CreateCall(memcmp_fn, {a_data, b_data, a_len}, "memcmp");
				auto *content_eq = builder->CreateICmpEQ(cmp_result, builder->getInt32(0), "content.eq");

				auto *result = builder->CreateAnd(len_eq, content_eq, "str.eq");
				if (b->op == TokenType::BANG_EQUAL)
					result = builder->CreateNot(result, "str.ne");
				return result;
			}
		}

		const bool is_unsigned = !left_sema->is_signed_integer();

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
		auto *opnd = emit_expr(u->operand);

		switch (u->op) {
			case TokenType::MINUS: return builder->CreateNeg(opnd, "neg");
			case TokenType::BANG: return builder->CreateNot(opnd, "not");
			case TokenType::STAR: {
				auto target_type = get_sema_type(u->operand);
				auto elem_type = target_type->pointee;
				auto *elem_llvm_type = get_llvm_type(elem_type);
				return builder->CreateLoad(elem_llvm_type, opnd, "deref");
			}
			default: return opnd;
		}
	}

	// 6. Function call, struct instantiation or method call: callee(args...)
	if (isa<CallExpr>(expr)) {
		const auto *c = as<CallExpr>(expr);

		// Static T.size() inquiry (e.g. Point.size(), i32.size(), str.size())
		if (analyzer && analyzer->resolved_type_sizes.contains(c)) {
			Semantic ty = analyzer->resolved_type_sizes.at(c);
			if (ty->kind == SemaType::VOID) {
				return builder->getInt64(0);
			}
			llvm::Type *llvm_ty = get_llvm_type(ty);
			uint64_t sz = module->getDataLayout().getTypeAllocSize(llvm_ty);
			return builder->getInt64(sz);
		}

		// 6a. Direct call or struct instantiation by name
		auto resolved_it = analyzer ? analyzer->resolved_symbols.find(c) : decltype(analyzer->resolved_symbols.find(c)){};
		bool has_resolved = analyzer && resolved_it != analyzer->resolved_symbols.end();

		if (isa<IdentifierExpr>(c->callee) || has_resolved) {
			std::string raw_name;
			if (isa<IdentifierExpr>(c->callee)) {
				raw_name = std::string(as<IdentifierExpr>(c->callee)->name);
			} else if (has_resolved) {
				raw_name = resolved_it->second;
			}
			std::string target_name = raw_name;
			if (has_resolved) {
				target_name = to_llvm_name(resolved_it->second);
			}

			// Struct instantiation: Point(10, 20)
			if (struct_types.contains(target_name) || (
				    analyzer && (analyzer->structs.contains(target_name) || (
					                 has_resolved && analyzer->structs.contains(
						                 resolved_it->second
					                 ))))) {
				llvm::Type *st_type = nullptr;
				if (auto it = struct_types.find(target_name); it != struct_types.end()) st_type = it->second;
				if (!st_type && has_resolved) {
					const auto &sym_name = resolved_it->second;
					if (auto it = struct_types.find(sym_name); it != struct_types.end()) st_type = it->second;
					if (!st_type) {
						if (auto it = struct_types.find(to_llvm_name(sym_name)); it != struct_types.end()) st_type = it->second;
					}
					if (!st_type && analyzer->structs.contains(sym_name)) {
						emit_instantiated_struct(sym_name);
						if (auto it = struct_types.find(to_llvm_name(sym_name)); it != struct_types.end()) st_type = it->second;
						if (!st_type) {
							if (auto it = struct_types.find(sym_name); it != struct_types.end()) st_type = it->second;
						}
					}
				}
				if (!st_type) {
					if (auto it = struct_types.find(raw_name); it != struct_types.end()) st_type = it->second;
				}

				llvm::Function *fn = builder->GetInsertBlock()->getParent();
				llvm::AllocaInst *tmp_st = create_entry_block_alloca(fn, st_type, "st_tmp");
				for (size_t i = 0; i < c->args.size(); ++i) {
					llvm::Value *arg_val = emit_expr(c->args[i]);
					llvm::Value *field_ptr = builder->CreateStructGEP(
						st_type, tmp_st, static_cast<unsigned>(i), "init_field"
					);
					builder->CreateStore(arg_val, field_ptr);
				}
				return builder->CreateLoad(st_type, tmp_st, "st_val");
			}

			// Regular function call
			auto *callee = module->getFunction(target_name);
			if (!callee && analyzer) {
				std::string fn_lookup = has_resolved
					                        ? resolved_it->second
					                        : target_name;
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
			for (size_t i = 0; i < c->args.size(); ++i) {
				auto *arg_val = emit_expr(c->args[i]);
				if (i < callee->getFunctionType()->getNumParams()) {
					auto *expected_ty = callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
					if (expected_ty->isPointerTy() && arg_val->getType()->isStructTy()) {
						arg_val = builder->CreateExtractValue(arg_val, 0, "str_ptr");
					}
				} else if (callee->isVarArg() && arg_val->getType()->isStructTy()) {
					arg_val = builder->CreateExtractValue(arg_val, 0, "str_ptr");
				}
				args.push_back(arg_val);
			}

			return builder->CreateCall(callee, args);
		}

		// 6b. Method call: object.method(args...)
		if (isa<MemberExpr>(c->callee)) {
			const auto *m = as<MemberExpr>(c->callee);
			auto obj_type = get_sema_type(m->object);

			// Built-in str methods
			if (obj_type->is_str()) {
				auto *str_val = emit_expr(m->object);
				if (std::string(m->member) == "size" || std::string(m->member) == "len") {
					return builder->CreateExtractValue(str_val, 1, "str.len");
				}
				if (std::string(m->member) == "c_str") {
					return builder->CreateExtractValue(str_val, 0, "str.cstr");
				}
				if (std::string(m->member) == "slice") {
					auto *str_ty = struct_types["str"];
					auto *fn = builder->GetInsertBlock()->getParent();
					auto *s_alloca = create_entry_block_alloca(fn, str_ty, "slice.s");
					builder->CreateStore(str_val, s_alloca);

					// Arg 0: start
					llvm::Value *start_val = emit_expr(c->args[0]);
					if (start_val->getType()->getIntegerBitWidth() < 64) {
						start_val = builder->CreateSExt(start_val, builder->getInt64Ty(), "start.i64");
					} else if (start_val->getType()->getIntegerBitWidth() > 64) {
						start_val = builder->CreateTrunc(start_val, builder->getInt64Ty(), "start.i64");
					}

					// Arg 1: end (or s.len if omitted)
					llvm::Value *end_val = nullptr;
					if (c->args.size() >= 2) {
						end_val = emit_expr(c->args[1]);
						if (end_val->getType()->getIntegerBitWidth() < 64) {
							end_val = builder->CreateSExt(end_val, builder->getInt64Ty(), "end.i64");
						} else if (end_val->getType()->getIntegerBitWidth() > 64) {
							end_val = builder->CreateTrunc(end_val, builder->getInt64Ty(), "end.i64");
						}
					} else {
						end_val = builder->CreateExtractValue(str_val, 1, "str.len");
					}

					auto *slice_fn = module->getFunction("__kobel_str_slice");
					return builder->CreateCall(slice_fn, {s_alloca, start_val, end_val}, "slice.res");
				}
				return nullptr;
			}

			std::string st_name = obj_type->is_struct() ? obj_type->struct_name : obj_type->pointee->struct_name;
			std::string mangled = to_llvm_name(st_name) + "_" + std::string(m->member);

			auto *callee = module->getFunction(mangled);
			if (!callee && analyzer && analyzer->structs.contains(st_name)) {
				emit_instantiated_struct(st_name);
				callee = module->getFunction(mangled);
			}
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
				const auto &fn_sym = analyzer->functions.at(mangled);
				if (!fn_sym.param_types.empty() && fn_sym.param_names[0] == "self") {
					const auto &self_expected = fn_sym.param_types[0];
					if (self_expected->is_pointer()) {
						if (obj_type->is_pointer()) {
							args.push_back(emit_expr(m->object));
						} else {
							args.push_back(emit_lvalue(m->object));
						}
					} else {
						args.push_back(emit_expr(m->object));
					}
				}
			}

			// Load remaining arguments
			for (size_t i = 0; i < c->args.size(); ++i) {
				auto *arg_val = emit_expr(c->args[i]);
				size_t param_idx = (analyzer && analyzer->functions.contains(mangled) && !analyzer->functions.
				                    at(mangled).param_types.empty() && analyzer->functions.at(mangled).param_names[0] ==
				                    "self")
					                   ? i + 1
					                   : i;
				if (param_idx < callee->getFunctionType()->getNumParams()) {
					auto *expected_ty = callee->getFunctionType()->getParamType(static_cast<unsigned>(param_idx));
					if (expected_ty->isPointerTy() && arg_val->getType()->isStructTy()) {
						arg_val = builder->CreateExtractValue(arg_val, 0, "str_ptr");
					}
				} else if (callee->isVarArg() && arg_val->getType()->isStructTy()) {
					arg_val = builder->CreateExtractValue(arg_val, 0, "str_ptr");
				}
				args.push_back(arg_val);
			}

			return builder->CreateCall(callee, args);
		}

		return nullptr;
	}

	// 7. Member access: object.field
	if (isa<MemberExpr>(expr)) {
		const auto *m = as<MemberExpr>(expr);

		// Resolved symbol (e.g. module-prefixed constant or enum member)
		if (analyzer) {
			if (auto it = analyzer->resolved_symbols.find(expr); it != analyzer->resolved_symbols.end()) {
				const auto &sym_name = it->second;
				size_t last_dot = sym_name.rfind('.');
				if (last_dot != std::string::npos) {
					std::string enum_part = sym_name.substr(0, last_dot);
					std::string member_part = sym_name.substr(last_dot + 1);
					if (auto it_enum = analyzer->enums.find(enum_part); it_enum != analyzer->enums.end()) {
						if (auto it_m = it_enum->second.member_values.find(member_part); it_m != it_enum->second.member_values.end()) {
							llvm::Type *llvm_ty = get_llvm_type(it_enum->second.underlying_type);
							return llvm::ConstantInt::get(llvm_ty, it_m->second);
						}
					}
				}
				std::string llvm_c_name = to_llvm_name(sym_name);
				if (auto it_g = global_consts.find(llvm_c_name); it_g != global_consts.end()) {
					return builder->CreateLoad(it_g->second->getValueType(), it_g->second, llvm_c_name);
				}
			}
		}

		// 7a. Enum member access (e.g. Color.RED)
		if (isa<IdentifierExpr>(m->object)) {
			const auto id_name = as<IdentifierExpr>(m->object)->name;
			if (auto it_enum = analyzer->enums.find(id_name); it_enum != analyzer->enums.end()) {
				if (
					auto it_m = it_enum->second.member_values.find(m->member);
					it_m != it_enum->second.member_values.end()
				) {
					llvm::Type *llvm_ty = get_llvm_type(it_enum->second.underlying_type);
					return llvm::ConstantInt::get(llvm_ty, it_m->second);
				}
			}
		}

		// 7b. .value property on enum variable (e.g. status.value)
		auto obj_sema = get_sema_type(m->object);
		if (obj_sema->is_enum() && m->member == "value") return emit_expr(m->object);

		// 7c. .len property on array (e.g. arr.len)
		if (obj_sema->is_array() && m->member == "len")
			return builder->getInt32(
				static_cast<int32_t>(obj_sema->array_size)
			);

		// 7d. str properties (.len, .data)
		if (obj_sema->is_str()) {
			auto *str_val = emit_expr(m->object);
			if (m->member == "len") {
				return builder->CreateExtractValue(str_val, 1, "str.len");
			}
			if (m->member == "data") {
				return builder->CreateExtractValue(str_val, 0, "str.data");
			}
		}

		llvm::Value *field_ptr = emit_lvalue(m);
		Semantic field_sema = nullptr;
		if (obj_sema->is_struct() || (obj_sema->is_pointer() && obj_sema->pointee && obj_sema->pointee->is_struct())) {
			std::string st_name = obj_sema->is_struct() ? obj_sema->struct_name : obj_sema->pointee->struct_name;
			if (analyzer && analyzer->structs.contains(st_name)) {
				const auto &sym = analyzer->structs.at(st_name);
				if (auto it = sym.field_types.find(m->member); it != sym.field_types.end()) {
					field_sema = it->second;
				}
			}
		}
		if (!field_sema) field_sema = get_sema_type(m);
		llvm::Type *field_llvm_type = get_llvm_type(field_sema);
		if (!field_ptr || !field_llvm_type) return nullptr;
		return builder->CreateLoad(field_llvm_type, field_ptr, std::string(m->member));
	}

	// 8. Array indexing: target[index]
	if (isa<IndexExpr>(expr)) {
		const auto *idx = as<IndexExpr>(expr);
		llvm::Value *elem_ptr = emit_lvalue(idx);
		auto elem_sema = get_sema_type(idx);
		llvm::Type *elem_llvm_type = get_llvm_type(elem_sema);
		if (!elem_ptr || !elem_llvm_type) return nullptr;
		return builder->CreateLoad(elem_llvm_type, elem_ptr);
	}

	// 9. Type cast: expr as TargetType
	if (isa<CastExpr>(expr)) {
		const auto *c = as<CastExpr>(expr);
		auto src_sema = get_sema_type(c->expr);
		auto dest_sema = analyzer->resolve_type(c->target_type);
		auto *dest_type = get_llvm_type(dest_sema);

		// Array decay: arr as *T
		if (src_sema->is_array() && dest_sema->is_pointer()) {
			if (auto *arr_lval = emit_lvalue(c->expr)) {
				auto *arr_ty = get_llvm_type(src_sema);
				return builder->CreateGEP(
					arr_ty, arr_lval,
					{builder->getInt32(0), builder->getInt32(0)},
					"arraydecay"
				);
			}
		}

		llvm::Value *val = emit_expr(c->expr);

		const bool src_is_int = src_sema->is_integer() || src_sema->is_enum();
		const bool dest_is_int = dest_sema->is_integer() || dest_sema->is_enum();

		if (src_is_int && dest_is_int) {
			const unsigned src_bits = val->getType()->getIntegerBitWidth();
			const unsigned dest_bits = dest_type->getIntegerBitWidth();
			if (src_bits == dest_bits) return val;
			if (dest_bits > src_bits) {
				bool is_signed = src_sema->is_signed_integer();
				if (src_sema->is_enum() && src_sema->underlying_type)
					is_signed = src_sema->underlying_type->is_signed_integer();
				return is_signed
					       ? builder->CreateSExt(val, dest_type, "sext")
					       : builder->CreateZExt(val, dest_type, "zext");
			}
			return builder->CreateTrunc(val, dest_type, "trunc");
		}

		if (src_sema->is_pointer() && dest_sema->is_pointer()) return val;

		if (src_sema->is_integer() && dest_sema->is_pointer())
			return builder->CreateIntToPtr(
				val, dest_type, "inttoptr"
			);

		if (src_sema->is_pointer() && dest_sema->is_integer())
			return builder->CreatePtrToInt(
				val, dest_type, "ptrtoint"
			);

		if (src_sema->is_char() && dest_sema->is_integer())
			return builder->CreateZExt(
				val, dest_type, "zext_char"
			);

		if (src_sema->is_integer() && dest_sema->is_char())
			return builder->CreateTrunc(
				val, dest_type, "trunc_char"
			);

		return val;
	}

	// 10. Group: (expr)
	if (isa<GroupExpr>(expr)) return emit_expr(as<GroupExpr>(expr)->expr);

	// 11. If-expression
	if (isa<IfExpr>(expr)) return emit_if_expr(as<IfExpr>(expr));

	// 12. When-expression
	if (isa<WhenExpr>(expr)) return emit_when_expr(as<WhenExpr>(expr));

	return nullptr;
}

llvm::Value *CodeGen::emit_if_expr(const IfExpr *expr) {
	if (!expr) return nullptr;

	llvm::Value *cond = emit_expr(expr->condition);
	llvm::Function *fn = builder->GetInsertBlock()->getParent();

	llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(*context, "ifexpr_then", fn);
	llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(*context, "ifexpr_else", fn);
	llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "ifexpr_merge");

	builder->CreateCondBr(cond, then_bb, else_bb);

	// Then branch
	builder->SetInsertPoint(then_bb);
	llvm::Value *then_val = emit_expr(expr->then_branch);
	llvm::BasicBlock *then_end_bb = builder->GetInsertBlock();
	bool then_reaches = false;
	if (!then_end_bb->hasTerminator()) {
		builder->CreateBr(merge_bb);
		then_reaches = true;
	}

	// Else branch
	builder->SetInsertPoint(else_bb);
	llvm::Value *else_val = emit_expr(expr->else_branch);
	llvm::BasicBlock *else_end_bb = builder->GetInsertBlock();
	bool else_reaches = false;
	if (!else_end_bb->hasTerminator()) {
		builder->CreateBr(merge_bb);
		else_reaches = true;
	}

	// Merge block
	fn->insert(fn->end(), merge_bb);
	builder->SetInsertPoint(merge_bb);

	auto result_sema = get_sema_type(expr);
	llvm::Type *res_llvm_type = get_llvm_type(result_sema);

	if (res_llvm_type->isVoidTy()) return nullptr;

	// Normalize types if needed
	if (res_llvm_type->isIntegerTy()) {
		if (then_val && then_val->getType() != res_llvm_type && then_val->getType()->isIntegerTy()) {
			if (then_reaches) {
				builder->SetInsertPoint(then_end_bb->getTerminator());
				then_val = builder->CreateIntCast(then_val, res_llvm_type, result_sema->is_signed_integer());
			}
		}
		if (else_val && else_val->getType() != res_llvm_type && else_val->getType()->isIntegerTy()) {
			if (else_reaches) {
				builder->SetInsertPoint(else_end_bb->getTerminator());
				else_val = builder->CreateIntCast(else_val, res_llvm_type, result_sema->is_signed_integer());
			}
		}
		builder->SetInsertPoint(merge_bb);
	} else if (res_llvm_type->isPointerTy()) {
		if (then_val && then_val->getType() != res_llvm_type) {
			if (then_reaches) {
				builder->SetInsertPoint(then_end_bb->getTerminator());
				then_val = builder->CreatePointerCast(then_val, res_llvm_type);
			}
		}
		if (else_val && else_val->getType() != res_llvm_type) {
			if (else_reaches) {
				builder->SetInsertPoint(else_end_bb->getTerminator());
				else_val = builder->CreatePointerCast(else_val, res_llvm_type);
			}
		}
		builder->SetInsertPoint(merge_bb);
	}

	unsigned incoming_count = (then_reaches ? 1 : 0) + (else_reaches ? 1 : 0);
	if (incoming_count == 0) return llvm::UndefValue::get(res_llvm_type);

	llvm::PHINode *phi = builder->CreatePHI(res_llvm_type, incoming_count, "ifexpr.res");
	if (then_reaches) phi->addIncoming(then_val, then_end_bb);
	if (else_reaches) phi->addIncoming(else_val, else_end_bb);

	return phi;
}

llvm::Value *CodeGen::emit_when_expr(const WhenExpr *expr) {
	if (!expr || expr->arms.empty()) return nullptr;

	auto result_sema = get_sema_type(expr);
	llvm::Type *res_llvm_type = get_llvm_type(result_sema);

	llvm::Function *fn = builder->GetInsertBlock()->getParent();
	llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context, "when_merge");

	llvm::Value *cond_val = nullptr;
	Semantic cond_sema = nullptr;
	if (expr->condition) {
		cond_val = emit_expr(expr->condition);
		cond_sema = get_sema_type(expr->condition);
	}

	std::vector<std::pair<llvm::Value *, llvm::BasicBlock *> > incoming_vals;

	for (size_t i = 0; i < expr->arms.size(); ++i) {
		const auto &arm = expr->arms[i];
		llvm::BasicBlock *arm_body_bb = llvm::BasicBlock::Create(*context, "when_arm_body", fn);

		if (arm.is_else) {
			builder->CreateBr(arm_body_bb);
			builder->SetInsertPoint(arm_body_bb);
			llvm::Value *body_val = emit_expr(arm.body);
			llvm::BasicBlock *body_end_bb = builder->GetInsertBlock();
			if (!body_end_bb->hasTerminator()) {
				if (res_llvm_type->isIntegerTy() && body_val && body_val->getType() != res_llvm_type && body_val->
				    getType()->isIntegerTy()) {
					body_val = builder->CreateIntCast(body_val, res_llvm_type, result_sema->is_signed_integer());
				} else if (res_llvm_type->isPointerTy() && body_val && body_val->getType() != res_llvm_type) {
					body_val = builder->CreatePointerCast(body_val, res_llvm_type);
				}
				builder->CreateBr(merge_bb);
				incoming_vals.emplace_back(body_val, body_end_bb);
			}
			break;
		}

		llvm::BasicBlock *next_arm_bb = (i + 1 < expr->arms.size())
			                                ? llvm::BasicBlock::Create(*context, "when_arm_next", fn)
			                                : llvm::BasicBlock::Create(*context, "when_unreachable", fn);

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
		llvm::Value *body_val = emit_expr(arm.body);
		llvm::BasicBlock *body_end_bb = builder->GetInsertBlock();
		if (!body_end_bb->hasTerminator()) {
			if (res_llvm_type->isIntegerTy() && body_val && body_val->getType() != res_llvm_type && body_val->getType()
			    ->isIntegerTy()) {
				body_val = builder->CreateIntCast(body_val, res_llvm_type, result_sema->is_signed_integer());
			} else if (res_llvm_type->isPointerTy() && body_val && body_val->getType() != res_llvm_type) {
				body_val = builder->CreatePointerCast(body_val, res_llvm_type);
			}
			builder->CreateBr(merge_bb);
			incoming_vals.emplace_back(body_val, body_end_bb);
		}

		if (i + 1 < expr->arms.size()) {
			builder->SetInsertPoint(next_arm_bb);
		} else {
			builder->SetInsertPoint(next_arm_bb);
			builder->CreateUnreachable();
		}
	}

	fn->insert(fn->end(), merge_bb);
	builder->SetInsertPoint(merge_bb);

	if (res_llvm_type->isVoidTy()) return nullptr;

	if (incoming_vals.empty()) return llvm::UndefValue::get(res_llvm_type);

	llvm::PHINode *phi = builder->CreatePHI(res_llvm_type, incoming_vals.size(), "when.res");
	for (const auto &[val, bb]: incoming_vals) {
		phi->addIncoming(val, bb);
	}
	return phi;
}
