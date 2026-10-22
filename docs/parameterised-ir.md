# Parameterised IR — kế hoạch

Mục tiêu: thay monomorphization bằng **clone AST** bằng biểu diễn **instance có tham số hoá**: một
instance là `(template, type arguments)` và được kiểm/emit mà không phải nhân bản toàn bộ cây AST.

Tài liệu này ghi trạng thái hiện tại, lý do phần còn lại khó, và các bước tiếp theo.

## 1. Hiện trạng (sau bước 1, `31b2a02`)

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
| 2 | `Instance` record + `instances_by_key` + call/struct/impl dùng key theo `*Type` | thay `inst_map` theo tên |
| 3 | Thân hàm dùng chung AST + side table annotation; `T.size()` thành node `SizeOfParam` hạ ở codegen | bước lớn nhất |
| 4 | Bỏ `clone_fn/clone_expr/clone_stmt`; `drain_instances` chỉ rewrite view/signature | xoá `mono_clone` |
| 5 | Instance graph: thứ tự emit theo phụ thuộc call (không cần prepend) | sau khi hết clone |

## 4. Rủi ro

- Bước 3 chạm mọi nơi đọc `type_ptr`; snapshot chương trình là lưới an toàn chính (C phải giữ nguyên).
- Bootstrap: mỗi bước đổi hành vi compiler đều cần binary thế hệ trước (xem §7 trong architecture).
- Nếu không đủ thời gian, dừng ở bước 2: dù vẫn clone function, bookkeeping instance đã sạch và dedupe
  theo type thật (không theo token chuỗi).
