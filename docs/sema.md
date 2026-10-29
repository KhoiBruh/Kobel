# Sema — bản đồ

Trang này là "bản đồ" của tầng ngữ nghĩa (`src/compiler/sema`): pass nào sở hữu dữ liệu gì, quyết định
được ghi vào đâu, và vòng đời của một instance generic. Đặc tả ngôn ngữ ở [`../dac-ta-ngon-ngu.md`](../dac-ta-ngon-ngu.md);
kiến trúc tổng thể ở [`architecture.md`](architecture.md) (§3.3, §6.7); kế hoạch/tình trạng monomorphization
ở [`parameterised-ir.md`](parameterised-ir.md).

## 1. Bốn bước và nơi chúng sống

| Bước | Entry | Module chính | Việc |
|---|---|---|---|
| 1 | `loader.load_program` | `loader` + `lexer` + `parser` | nạp module theo `use`, lex/parse từng tệp thành AST |
| 2 | `decl.collect.collect_program(prog)` | `decl.collect`, `decl.pass`, `mono.*` | gom khai báo, forward-declare struct, giải kiểu, instantiate generic, drain worklist, append instance sau program |
| 3 | `body.program.check_program(prog)` | `body.*`, `desugar.*` | kiểm thân hàm theo từng instance (side table), hạ đường `for`/interp; flush instance sinh muộn |
| 4 | `lower.lower_program(prog)` | `lower` | materialize thân instance, hạ sugar còn lại thành dạng C phát được |
| — | `codegen.gen_program(prog)` | `codegen.*` | chỉ đọc AST + `type_ptr`/`resolved_*`, không suy luận kiểu |

Thứ tự gọi nằm ở `src/compiler/driver/pipeline.kb`.

## 2. Bố cục module

```
sema/
├─ body/     access  call  expr  interp  pass  pattern  program  stmt
├─ decl/     pass (core)  collect (driver)  type (resolve_ast_type)
├─ mono/     registry (template tables + worklist)  instantiate  rewrite  clone
├─ desugar/  loop (for)  interp (chuỗi)
├─ types.kb      DataType / Type / TypePayload / payload pointer-array-fn
├─ methods.kb    MethodInfo / PrimMethods
├─ structs.kb    StructField / StructType (+ layout)
├─ enums.kb      EnumInfo / EnumMemberInfo
├─ prims.kb      PrimTypes + bảng tên & C-size nguyên thuỷ (+ literal suffix)
├─ type_ast.kb   ast_type_from_type (kiểu đã giải -> annotation AST)
├─ template.kb   GenTemplate / GenSubst / TemplateKind        (lá)
├─ instance.kb   InstanceKind / AnnEntry / InstanceRecord
├─ traits.kb     TraitInfo / TraitMethod                      (lá)
├─ symbol.kb     Symbol / Scope / ModuleScope / GenericReg / SymbolTable
└─ lower.kb      hạ sugar cho backend
```

`types` ↔ `methods`/`structs`/`enums` là **vòng pointer-only** (mọi field bắc cầu là `*T` hoặc `List<*T>`),
nên forward declaration ở `types.kb` là đủ; không có phụ thuộc by-value nào đi ngược vòng.

Quy ước: module path = đường dẫn tệp (`sema/body/pass.kb` → `compiler.sema.body.pass`). Segment phải là
`IDENTIFIER` nên không dùng được `for`, `trait`… (`desugar/loop.kb`, `traits.kb`).

## 3. Ai sở hữu dữ liệu gì

| Dữ liệu | Nơi định nghĩa | Ai ghi |
|---|---|---|
| AST node | `ast/*` | loader/parser tạo; sema **không đổi `kind`/`data`** (I1) |
| `*Type` + payload | `sema/types.kb` (payload struct ở `methods/structs/enums`) | `DeclPass` (giải kiểu) qua `impl Arena` constructors; annotation AST → `*Type` ở `sema/decl/type.kb` |
| `PrimTypes` (16 instance chuẩn) | `sema/prims.kb` | `SymbolTable` tạo một lần; shorthand `t_*`; `impl <primitive>` thêm method vào `Type.methods` |
| `MethodInfo` / `PrimMethods` | `sema/methods.kb` | `collect_impl` (decl.pass) |
| `StructField` / `StructType` | `sema/structs.kb` | `decl.pass` (field/type), `layout_struct` tính offset/size |
| `EnumInfo` / `EnumMemberInfo` | `sema/enums.kb` | `decl.pass` (enum) |
| `Symbol` / `Scope` / `ModuleScope` | `sema/symbol.kb` | `DeclPass` (collect), `BodyPass` chỉ đọc + mở/đóng scope |
| `GenTemplate` | `sema/template.kb` | `decl.collect` đăng ký (pass 0) |
| `GenericReg` | `sema/symbol.kb` | template tables, `inst_map` (tên C → `Symbol`), `records` (identity theo kiểu), `instances` (worklist) |
| `InstanceRecord` / `AnnEntry` | `sema/instance.kb` | `mono.instantiate` tạo record; `BodyPass` ghi annotation per-instance vào `ann_map` (`HashMap<usz, *AnnEntry>`, tra O(1)) |
| `TraitInfo` / `TraitMethod` | `sema/traits.kb` | `decl.collect` gom, `decl.pass.apply_trait` đối chiếu |

`SymbolTable` là điểm truy cập duy nhất: `symtab.gen` (registry), `symtab.traits`, `symtab.assoc_types`,
`symtab.prims` (primitive singleton).

## 4. Kênh quyết định (chỉ có bốn cửa)

Khi `BodyPass.current_rec == null` (khai báo thường) quyết định ghi thẳng lên node; khi đang kiểm một
instance (`current_rec != null`) quyết định đi vào `InstanceRecord.ann_map` để materialization áp lên bản sao.

| Cửa | Ghi gì | Ai đọc |
|---|---|---|
| `annotate_type(node, ty)` | `type_ptr` — kiểu đã giải của biểu thức | `lower`, `codegen` |
| `set_resolved(node, r)` | `resolved_node` — node thay thế (enum member, `T.size()`, `for` block, call đã instantiate) | `lower`, `codegen` |
| `set_method(node, mi)` | `resolved_method` — method/operator đã chọn | `lower` |
| `set_ident_name(node, name)` | tên C đã mangle của identifier | `mono.clone` khi materialize |

Ngoài ra `AnnEntry` còn giữ các quyết định phụ (không phải kênh chung): `list_name`/`elem_node` (List
literal), `obj_replace` (`(*p)` trước member access), `var_ty` (annotation suy ra cho biến), `init_override`
(tham số kiểu lan vào initializer).

**Không** gán thẳng `node.type_ptr`/`node.resolved_*`/`id.name` trong pass kiểm — luôn đi qua bốn cửa trên.

## 5. Vòng đời một instance generic

1. **Pass 0** (`decl.collect`): template `<T>` được đăng ký vào `symtab.gen` (`GenTemplate`).
2. **Pass 2** (`decl.collect` + `mono.rewrite`): gặp `List<i32>` / `f<i32>(...)` → `instantiate_struct` /
   `instantiate_fn`:
   - giải type arg thành `*Type`, tra `find_instance_record(template, args)` (identity theo kiểu, không theo tên);
   - tạo **view** (struct view hoặc `FnDecl`/`ImplDecl` view) có signature đã thay thế, **body dùng chung AST template**;
   - tạo `InstanceRecord` + `sig` (FnType riêng), gắn `AstNode.instance`;
   - `queue_decl` append cặp `(module marker, decl)` vào `gen.instances`.
3. **`drain_instances`**: rewrite các instance vừa tạo tới fixpoint (generic use bên trong instance), rồi
   `collect_program` **append** batch này sau program (creation order = dependency order).
4. **`infer_arrow_returns`** (`body.program`): gom arrow fn chưa có return type; worklist **nở dần** theo
   instance sinh muộn trong lúc inference; mọi kênh call (`new`, method, index-`get`) báo `infer_blocked`
   khi callee còn pending để attempt được retry, không cache `none`.
5. **Check thân** (`body.pass` + `body.*`): `push_instance(rec)` thay env tham số kiểu, kiểm trên AST template,
   quyết định vào `ann`; `pop_instance` trả env.
6. **Flush** (`body.program.flush_instances`): instance sinh trong lúc check body (generic method) được kiểm
   + append nốt.
7. **Lower** (`lower`): `materialize_body(rec)` clone body template và áp `ann` lên bản sao, rồi hạ sugar trên
   cây riêng đó; template không bao giờ bị lower.
8. **Codegen**: chỉ đọc AST đã có `type_ptr`/`resolved_*`.

## 6. Bất biến & cạm bẫy

- **AST template bất biến** sau parse: mọi quyết định per-instance đi qua side table (§4). Đây là I1.
- **Instance identity theo `(template, args: List<*Type>)`**, không theo tên C; tên chỉ để emit. `Type.equals`
  so struct theo `c_name` đã mangle.
- **Codegen không suy luận kiểu** (I2): thiếu `type_ptr` là lỗi sema, không phải việc của backend.
- **Không prepend**: instance được append; prototype + struct-sort của codegen lo thứ tự C.
- **Sửa tên C của identifier** chỉ qua `set_ident_name`; nếu gán thẳng, instance sau sẽ nhìn thấy tên của
  instance trước (đã từng là bug, xem `parameterised-ir.md` §5).
- **Đừng đổi `kind`/`data` của node** trong `check_*` (P2). Hạ cấp = tạo node mới qua `set_resolved` hoặc
  trong `lower`.
- **Env tham số kiểu thay thế, không xếp chồng**: `push_instance_env` save/restore cả list; lookup lấy mục
  cuối (innermost).

## 7. Thêm một cú pháp mới — checklist

1. `lexer/token.kb`: token mới (nếu cần).
2. `parser/*`: parse thành `AstNode`; payload struct khai báo trước ở `ast/node.kb`, định nghĩa ở
   `ast/{expr,stmt,decl}.kb`; thêm accessor `as_*` + factory `impl Arena`.
3. `decl.collect`/`decl.pass`: đăng ký/giải kiểu nếu cú pháp xuất hiện ở khai báo.
4. `body.*`: `check_*` gắn `type_ptr` (và `resolved_*` nếu cần thay node); hạ cấp để lại cho `lower`.
5. `lower.kb`: sinh dạng C phát được nếu cú pháp không đi thẳng tới backend.
6. `docs/architecture.md` §6.2 (bảng hạ cấp) + `docs/sema.md` nếu thêm kênh quyết định.
7. Test: `tests/features/` cho hành vi, `snap` cho C; chạy `scripts/check.ps1`.

## 8. Nợ kỹ thuật đã biết

- Đã gộp `PtrMap` và `HashMap` thành `HashMap<K: Hash, V>` dùng chung trait `Hash`. Đã loại bỏ hoàn toàn `PtrMap`.


