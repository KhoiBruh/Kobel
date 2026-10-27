# Parameterised IR — kế hoạch

Mục tiêu: thay monomorphization bằng **clone AST** bằng biểu diễn **instance có tham số hoá**: một
instance là `(template, type arguments)` và được kiểm/emit mà không phải nhân bản toàn bộ cây AST.

Tài liệu này ghi trạng thái hiện tại, lý do phần còn lại khó, và các bước tiếp theo.

## 1. Hiện trạng (sau bước 2, `753fe07`)

Đã parameter hoá **struct**:

- `instantiate_struct` nhận type argument, giải chúng thành `*Type`, đặt tên instance từ token chuẩn
  (struct/enum dùng C name đã mangle), tạo `StructType` rồi giải field type của template với
  `DeclPass.subst_names/subst_types` (type param → `*Type`).
- `resolve_ast_type` tra env này trước; khi env đang bật nó **không sửa AST annotation**, nên node của
  template được chia sẻ nguyên vẹn giữa các instance.
- Không còn `clone_struct`/`collect_struct_as`. Backend chỉ nhận một **view `StructDecl`** sinh từ field
  type đã giải (view này không bị kiểm lại).
- Thân struct được emit theo **thứ tự phụ thuộc by-value** (struct/enum payload), nên instance có thể
  nhúng struct khai báo sau nó.

**Instance có identity theo kiểu** (bước 2, `753fe07`):

- `GenericReg.records` giữ `InstanceRecord { kind, template, args: List<*Type>, c_name, decl }`.
- `find_instance(template, args)` so bằng `Type.equals`; tra record **trước**, tên C chỉ để emit.
- Sửa kèm bug tiềm ẩn: `Type.equals` so struct theo **source name**, nên hai struct cùng tên ở hai module
  bị coi là bằng nhau (typed lookup trả nhầm instance `List<StructField>` của module khác). Nay struct so
  theo `c_name` đã mangle.

Vẫn còn clone với **function/method**: `instantiate_fn` → `clone_fn` → `clone_stmt/clone_expr` sao chép
thân hàm và thay type arg bằng AST mới. Đó là phần "instance graph" còn lại.

## 2. Vì sao phần function bodies khó

1. **Annotation theo node**: sema ghi `AstNode.type_ptr` / `resolved_method` / `resolved_node` trực tiếp
   trên node. Nếu mọi instance dùng chung một cây AST template thì không thể lưu type riêng cho từng
   instance trên cùng node.
2. **Giá trị phụ thuộc type param**: `T.size()` hiện được hạ lúc clone (`clone_size_query`) vì kích thước
   chỉ biết khi có type arg. IR có tham số hoá phải biểu diễn "sizeof của tham số" và hạ ở codegen.
3. **Generic method call bên trong thân clone**: `clone_template_call` instantiate tại thời điểm clone;
   nếu thân được kiểm một lần thì call này phải trở thành "call tới template với substitution", không
   phải call tới instance đã biết tên.
4. **Codegen đọc AST**: `c_type_from_ast` cho annotation; nếu dùng lại AST template thì codegen cần
   substitution khi phát C.

## 3. Thiết kế đề xuất

### 3.1 Instance record

```
struct Instance {
    template: *GenTemplate,
    args: List<*Type>,        // đã resolve
    kind: STRUCT | FN | IMPL,
    c_name: str,              // token chuẩn
    view: *AstNode?           // chỉ cho struct (C emitter)
}
```

`GenericReg` giữ `instances_by_key` thay cho `inst_map` theo tên; key là `(template, args)` so sánh theo
`Type.equals`. Struct giữ view; function chỉ giữ `FnType` đã thay thế.

### 3.2 Typed side table cho thân hàm

Bảng `annotations: HashMap<NodeSite>` với key `(node, instance_id)`; `BodyPass` khi kiểm thân cho instance
ghi vào bảng thay vì `node.type_ptr`. `codegen` tra bảng theo `(node, instance)` đang emit.

Đây là thay đổi lớn: mọi chỗ đọc `node.type_ptr` phải nhận thêm instance hiện hành. Có thể bắt đầu bằng
`CCodeGen.current_instance: usz`, và một helper `type_of_node(node)` đọc `node.type_ptr` nếu instance vô
danh, ngược lại đọc bảng.

### 3.3 Kiểm thân một lần với type param "opaque"

Hai lựa chọn:

- **A. Check per-instance, no clone**: dùng side table + substitution env khi kiểm; mỗi instance kiểm lại
  thân template (như hiện tại) nhưng không clone AST. Ít rủi ro hơn, vẫn O(#instance) công kiểm.
- **B. Check once with opaque params**: thêm `Type` kind `PARAM` (có bound), hoãn `sizeof`, method call
  trên param phân giải qua bound. Nhanh hơn nhưng cần nhiều luật mới, và C vẫn phải emit per instance.

Đề xuất đi A trước (bước 2-3), B là tối ưu sau.

### 3.4 Các bước

| Bước | Nội dung | Ghi chú |
|---|---|---|
| 1 | Struct instantiation theo type | **xong** (`31b2a02`) |
| 2 | `InstanceRecord` + tra theo `(template, args: List<*Type>)`; tên C chỉ để emit | **xong** (`753fe07`, kèm fix `Type.equals` theo c_name) |
| 3 | Thân hàm dùng chung AST + side table annotation; `T.size()` thành node `SizeOfParam` hạ ở codegen | **xong** (`…` bước 3): view-based function/method instantiation, per-node side table, `SizeOfParam` hạ ở codegen; fixpoint byte-identical (xem §5) |
| 4 | Bỏ clone lúc instantiate; `drain_instances` chỉ rewrite view/signature; dọn `mono_clone`/`monomorphize` chết | **xong** (`…` bước 4): xoá `clone_impl`, `type_token_list`, `ast_type_token` (dead); `materialize_body` giữ vai trò **emission copy** (clone thân template + gán annotation từ side table) — không còn clone nào ở thời điểm instantiate |
| 5 | Instance graph: thứ tự emit theo phụ thuộc call (không cần prepend) | **một phần** (`…` bước 5): `flush_instances` (batch method-instance) không còn prepend — được append ở cuối program, vẫn đúng vì creation order = dependency order + prototype/struct-sort của codegen. Batch decl-pass (struct/function instance) **vẫn prepend** vì phát hiện order-sensitivity tiềm ẩn của sema: append batch này làm hỏng check src/main.kb (lỗi `when is` subject untyped, `params()` arm-type mismatch) — ghi chú §5 |

## 4. Rủi ro

- Bước 3 chạm mọi nơi đọc `type_ptr`; snapshot chương trình là lưới an toàn chính (C phải giữ nguyên).
- Bootstrap: mỗi bước đổi hành vi compiler đều cần binary thế hệ trước (xem §7 trong architecture).
- Nếu không đủ thời gian, dừng ở bước 2: dù vẫn clone function, bookkeeping instance đã sạch và dedupe
  theo type thật (không theo token chuỗi).

## 5. Ghi chú bước 3 (2026-10, đã hợp nhất)

Bước 3 đã được triển khai theo "phương án A" (§3.3) và hợp nhất (cùng bản golden C cập nhật). Nội dung
dưới đây ghi lại thiết kế cuối cùng và các trục trặc đã tìm ra trong quá trình làm, để việc bảo trì sau này
biết đường dây.

**Thiết kế cuối cùng (đã commit):**

- `InstanceRecord.ann: List<*AnnEntry>` — side table theo node (key = con trỏ node, tìm tuyến tính).
- `AstNode.instance: *u8` — link decl view → record.
- Node mới `EXPR_SIZE_OF_PARAM` (`SizeOfParam { param_index, size_text }`) + builder + codegen nhận.
- `instantiate_fn`/impl-loop của `instantiate_struct` tạo **view** `FnDecl`/`ImplDecl` (signature đã
  substitution, body = AST template dùng chung), không clone body; `drain_instances` bỏ qua fn/impl view.
- `BodyPass`: `current_rec` + `ann_entry/set_resolved/set_method/annotate_type` route vào side table khi
  đang check instance; env type-param lúc check instance **thay thế** (không xếp chồng) `decl.subst_*`.
- `lower_program`: template không được lower; với fn/impl view thì materialize qua
  `mono_clone.materialize_body` (clone body có gán annotation từ side table) rồi lower trên cây riêng.
- `check_call_expr`: gọi generic tường minh (`f<X>(...)`, `List<T>(...)`) trong template resolve ngay lúc
  check; `T.size()` hạ thành `SizeOfParam` (cỡ C tính từ type arg, giữ nguyên text literal như cũ).

**Trục trặc (có trace chứng minh):**

1. *Env chồng nhau gây shadowing.* Khi body của instance A check lồng instance B, `subst_names/subst_types`
   trên cùng `DeclPass` chồng lên; lookup tên trùng (vd `T`) trả mapping của lớp NGOÀI. Sửa bằng cách
   **thay thế** env khi vào instance (save/restore nguyên list) — phần này đã xử lý được.
2. *Chuỗi instantiate khác instance bị "chảy" type.* Với `List<i32>()` ở top-level, symbol
   `std__collections__list__List_i32` nhìn đúng nhưng `new` return type của List_i32 trả về `List_str`.
   Các điểm đã kiểm chứng: `instantiate_struct` gọi đúng args (`str`/`i32`), `collect_impl` đăng ký đúng
   per-instance methods (`ret=none` rồi arrow-inference điền), `check_impl` nhìn đúng struct
   (`List_i32`, fields=3). Tuy nhiên method-view của instance thứ hai không đi qua
   `infer_arrow_returns`/`check_fn_body` (không có trace `INF`/`BODY` cho `List_i32_*`), và kết quả
   `new` ở call site trả type của instance khác. Hướng nghi vấn: thứ tự merge `instances` vào `prog`
   (drain/flush) làm iview bị check theo module/scope sai, hoặc method view không có mặt trong
   `prog` lúc chạy `infer_arrow_returns` nên giữ nguyên ret type từ **collect** của view khác.
3. Hệ quả: vô số lỗi lệch type (`List_ptr_AstNode <- WhenArm`, `List_str <- i32`, alloc_val param bị
   type `Token`...). Không tìm ra gốc trong thời gian thử; đã revert sạch (git checkout src) và xác nhận lại
   build/test/snap/fixpoint xanh.

**Gợi ý cho lần sau:** (a) in trace tại `collect_program` pass 4 + `infer_arrow_returns` + `flush_instances`
xem iview có trong `prog` và được infer không; (b) kiểm tra `find_inst`/`inst_map` có bị ghi đè symbol
cùng tên giữa các instance không; (c) cân nhắc đưa `fn_type` của method view vào chính `InstanceRecord`
(thay vì để `collect_impl` dựng lại) để tránh mọi đường alias; (d) thử `materialize_body` sớm hơn (ngay sau
check từng instance) thay vì tách hẳn sang lower.

**Ba gốc rễ tìm được (và cách xử lý):**

1. *Env chồng gây shadowing.* Lúc body của instance A check lồng instance B, `subst_names/subst_types`
   trên cùng `DeclPass` chồng lên; lookup tên trùng (vd `T`) trả mapping của lớp NGOÀI. Sửa: khi vào
   instance, env được **thay thế** (save/restore nguyên list) — `BodyPass.push_instance_env/pop_instance_env`.
2. *Type node bị rename vĩnh viễn trên AST dùng chung.* `resolve_ast_type` và `check_ident_expr` từng đổi
   tên node `Self`/`Self.Item` sang C name của instance ĐẦU TIÊN; mọi instance sau nhìn thấy tên đã đổi nên
   giải sai. Sửa: trong môi trường instance (env đang bật / `current_rec != null`) KHÔNG rename node — kiểu
   đã giải vẫn được trả về, còn materialization thay annotation bằng `ast_type_from_type`.
3. *Container type-param dùng "match đầu tiên".* Khi vài mục cùng tên tồn tại trên stack env (tàn dư của
   instantiation lồng), lookup phải dùng **mục cuối** (innermost) chứ không phải mục đầu.
4. *Trùng tên token.* `find_instance`/tên C của function instance phải dùng token chuẩn từ **type đã giải**
   (struct/enum dùng `c_name` mangle, pointer/array đệ quy), nếu không cùng instance được đặt hai tên khác
   nhau và phát sinh bản sao.

**Kết quả cổng sau khi hoàn tất:** build từ seed OK; `test` 23/23 PASS; `snap --update` rồi `snap`
24/24 khớp; `fixpoint` cho `stage1.c == stage2.c` byte-identical (≈2.21MB). Phần C sinh ra đổi so với bản
gốc (tên instance dùng token chuẩn từ type đã giải; `T.size()` qua node `SizeOfParam` nhưng vẫn in literal
y hệt cũ) — nên golden được cập nhật cùng commit mã nguồn, đúng quy trình §8.1 của architecture.md.

**Ghi chú bước 5 (2026-10, một phần + điều tra).** Đã bỏ prepend cho batch `flush_instances` (method-instance sinh
trong body check): các instance này được **append** sau program và vẫn đúng vì creation order là dependency
order và codegen có prototype + struct-sort. Ngược lại, thử append batch decl-pass (`collect_program` pass 4)
cho thấy sema đang có order-sensitivity: compile src/main.kb (self-host) hỏng. Quá trình điều tra (trace
`IST`/`CTOR`) cho thấy cơ chế chính: **lazy instantiation trong lúc check**. Khi instance generic chỉ được
instantiate muộn (lúc đang check body, kể cả trong `infer_arrow_returns`), view của nó chưa nằm trong danh
sách pending của arrow-inference, nên `new` dạng arrow `=> Self(...)` chưa được suy luận return type → mọi
chỗ dựng `List<X>()` trả về `none`, gây loạt lỗi dây chuyền. Ba phương án đã thử: (a) flag `infer_blocked`
khi gặp callee pending (chưa đủ vì ở step 2 không có vòng retry); (b) eager check callee (`ensure_callee_ready_node`)
kèm re-scan pending sau mỗi vòng inference (bước tiến lớn: hết lỗi monomorphize/decl_pass/desugar_for, nhưng
vẫn còn lỗi dây chuyền khác tại fmt.comments/symbol/decl_collect — dấu hiệu còn root khác, có thể là
annotation chồng trên node dùng chung khi eager check chen ngang check chính thức); (c) giữ prepend cho batch
decl-pass. Chốt hiện tại: (c) — prepend được giữ, kèm comment trong mã; hướng tiếp theo nên điều tra triệt để
mọi chỗ còn đọc/ghi `AstNode` trực tiếp khi đang check instance (vd rename identifier sang c_name trong
`check_ident_expr` chưa qua side table) trước khi thử lại append.

**Điều tra vòng 2 (cùng ngày):** lặp lại thí nghiệm append với (a)+(b). Kết quả: lỗi giảm còn đúng hai
họ (fmt.comments `peek()`: `List<Comment>` element trả `none`; symbol.kb `lookup_current/lookup`: iterating
`List<*Symbol>` element trả `none`) kèm cascade. Đã thử thêm trace `UNKT` (resolve_ast_type thất bại) —
**không có** dòng nào được ghi: tức mọi tên kiểu vẫn resolve thành công, chứng tỏ `none` KHÔNG đến từ
lookup thất bại mà từ **env substitution trả `t_none()`** hoặc từ việc field type của struct instance được
giải trong lúc env/self chưa đúng (vd `List<Comment>` được instantiate khi `subst` đang bật và tên trùng
param, hoặc `current_self_type`/`current_module` lệch). Mốc điều tra tiếp theo: in args[0]/field[0] ngay
trong `instantiate_struct` cho template `List` khi first=Comment/Symbol tại thời điểm instantiate (xem có đang
nằm trong env/self nào không), và truy vết nơi gọi `instantiate_template_struct` đầu tiên cho cặp đó.
