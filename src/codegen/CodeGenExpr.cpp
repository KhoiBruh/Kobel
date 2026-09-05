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
	if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
		raw = raw.substr(1, raw.size() - 2);
	}
	std::string result;
	for (size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] == '\\' && i + 1 < raw.size()) {
			switch (const char next = raw[i + 1]) {
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

char unescape_char(std::string_view raw) {
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

} // anonymous namespace

// ============================================================================
// 1. LValue Evaluation (Địa chỉ gán)
// ============================================================================

llvm::Value* CodeGen::emit_lvalue(const Expr* expr) {
	if (isa<IdentifierExpr>(expr)) {
		const auto* id = as<IdentifierExpr>(expr);
		const auto name = std::string(id->name);

		if (const auto it = local_vars.find(name); it != local_vars.end()) return it->second;

		if (const auto it_g = global_consts.find(name); it_g != global_consts.end()) return it_g->second;

		return nullptr;
	}

	if (isa<MemberExpr>(expr)) {
		const auto* m = as<MemberExpr>(expr);
		auto obj_type = get_sema_type(m->object.get());
		const std::string st_name = obj_type.is_struct() ? obj_type.struct_name : obj_type.pointee->struct_name;

		llvm::Value* obj_ptr = nullptr;
		if (obj_type.is_pointer())obj_ptr = emit_expr(m->object.get());
		else obj_ptr = emit_lvalue(m->object.get());

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
		llvm::Value* ptr_val = emit_expr(idx->target.get());
		llvm::Value* index_val = emit_expr(idx->index.get());

		auto target_sema = get_sema_type(idx->target.get());
		llvm::Type* elem_llvm_type = get_llvm_type(*target_sema.pointee);

		return builder->CreateGEP(elem_llvm_type, ptr_val, index_val, "arrayidx");
	}

	if (isa<UnaryExpr>(expr) && as<UnaryExpr>(expr)->op == TokenType::STAR) {
		return emit_expr(as<UnaryExpr>(expr)->operand.get());
	}

	return nullptr;
}

// ============================================================================
// 2. Biểu thức (Expressions)
// ============================================================================

llvm::Value* CodeGen::emit_expr(const Expr* expr) {
	if (!expr) return nullptr;

	// 1. Hằng số (Literals)
	if (isa<LiteralExpr>(expr)) {
		const auto* lit = as<LiteralExpr>(expr);
		switch (lit->literal_kind) {
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

	// 2. Biến / Định danh (Identifiers)
	if (isa<IdentifierExpr>(expr)) {
		const auto* id = as<IdentifierExpr>(expr);
		const auto name = std::string(id->name);

		if (auto it = local_vars.find(name); it != local_vars.end()) {
			llvm::AllocaInst* alloca = it->second;
			return builder->CreateLoad(alloca->getAllocatedType(), alloca, name);
		}

		if (auto it_g = global_consts.find(name); it_g != global_consts.end()) {
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

		if (!callee && analyzer) {
			const auto it = analyzer->functions.find(std::string(fn_name));
			if (it != analyzer->functions.end()) {
				std::vector<llvm::Type*> param_types;
				for (const auto& param_type : it->second.param_types) {
					param_types.push_back(get_llvm_type(param_type));
				}
				llvm::Type* ret_type = get_llvm_type(it->second.return_type);
				llvm::FunctionType* fn_type = llvm::FunctionType::get(ret_type, param_types, false);
				callee = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, std::string(fn_name), *module);
			}
		}

		if (!callee) return nullptr;

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
