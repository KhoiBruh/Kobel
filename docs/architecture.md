# Kiến trúc Kobel

Tài liệu này mô tả kiến trúc **hiện tại** của dự án, các quy ước/bất biến đã hình thành trong
quá trình self-host, và **bố cục đích** kèm lộ trình tái cấu trúc.

Đặc tả ngôn ngữ nằm ở [`../dac-ta-ngon-ngu.md`](../dac-ta-ngon-ngu.md) (dự kiến chuyển thành `docs/spec.md`).

---

## 1. Tổng quan

Dự án có **hai trình biên dịch** cho cùng một ngôn ngữ (Kobel):

| | **v0 — seed** | **v1 — self-hosted** |
|---|---|---|
| Ngôn ngữ viết | C++20 + LLVM | Kobel |
| Nguồn | nhánh `v0-cpp` (C++/LLVM) | `src/main.kb` + `src/compiler/**` |
| Backend | LLVM IR → native | sinh **C99** → gọi C compiler (`cl` mặc định) |
| LOC | ~8 200 (+ ~4 500 test C++) | ~5 900 (+ 450 stdlib) |
| Vai trò | **hạt giống**: chỉ cần đủ hiểu ngôn ngữ hiện tại để build v1 | **trình biên dịch thật**, là nguồn sự thật về ngôn ngữ |
| Trạng thái | đóng băng dần (không nhận tính năng mới) | phát triển |

Vòng đời:

```
v0 (C++/LLVM)  ──biên dịch──▶  kobel_v1.exe
kobel_v1.exe <src>            ──▶ tự biên dịch chính nó ──▶ C99 ──▶ kobel_v2.exe
kobel_v2.exe  <src>           ──▶ output C99 **trùng byte** với v1   → FIXPOINT
```

Đích cuối (xem §9) là **bỏ hẳn v0/LLVM**, lấy chính output C của v1 làm hạt giống.

---

## 2. Sơ đồ tổng thể

```
                ┌─────────────── stdlib (lib/std) ───────────────┐
                │  io.kb  sys.kb  ascii.kb                        │
                │  collections/{list,hash_map,string_builder}.kb  │
                │  mem/{alloc,arena}.kb                           │
                └───────────────▲─────────────────────────────────┘
                                │  (chỉ phụ thuộc 1 chiều)
        ┌───────────────────────┴───────────────────────────────┐
        │                 compiler v1 (src/compiler)             │
        │  main → loader → lexer → parser → ast → sema → codegen │
        └───────────────────────┬───────────────────────────────┘
                                │ sinh C99
                          C compiler (cl/clang/gcc)
```

---

## 3. Kiến trúc v1 (trình biên dịch thật)

### 3.1 Pipeline (`src/compiler`, entry `src/main.kb`)

v1 chỉ có **4 bước**, in ra dưới dạng `[n/4]`:

| Bước | Gọi | Việc |
|---|---|---|
| 1 | `load_program(loader, input_path, src)` | `loader` nạp entry + mọi `use` (đệ quy), mỗi module: lex → parse → AST |
| 2 | `collect_program(decl_collect, prog)` | thu thập khai báo, đăng ký **template** generic, giải kiểu, **instantiate** generic, monomorphize (core nằm ở `decl_pass`) |
| 3 | `check_program(body_program, prog)` | kiểm tra thân hàm, **desugar** (xem §6.2), gắn kiểu cho biến nội bộ (core nằm ở `body_pass`) |
| 4 | `gen_program(c_program, prog)` | sinh C99 rồi `write_file`; nếu không `-emit-c` thì gọi C compiler + chạy (core nằm ở `c_codegen`) |

**Module search roots** (giống v0): thư mục file entry → các root `-I` → `lib` → `src`.

### 3.2 CLI

```
kobel [options] <source.kb>
  -o <file>        tên file thực thi đầu ra
  -emit-c <file>   chỉ sinh C99 rồi dừng
  --cc <compiler>  C compiler backend (mặc định: cl)
  -I <dir>         thêm thư mục tìm module
  -v, --version
  -h, --help
```

### 3.3 Đồ thị phụ thuộc module (một chiều, không vòng)

```
std.*                       (lá — không phụ thuộc compiler)
  ▲
  │
compiler.lexer.token       ← util.strutil
compiler.ast.node          │
compiler.ast.{types,expr,stmt,decl}   ← node, lexer.token, std.list
compiler.lexer.lexer       ← lexer.token, std.ascii, std.list
compiler.ast.builder       ← ast.*, lexer.token, std.list, std.arena
compiler.parser.*          ← lexer.token, ast.*, ast.builder, std.list, std.arena
compiler.sema.types        ← std.list, std.arena
compiler.sema.symbol       ← sema.types, ast.node, std.list, std.arena
compiler.sema.decl_pass    ← ast.*, ast.builder, sema.{types,symbol}, strutil, std   (core + monomorph)
compiler.sema.decl_collect ← sema.decl_pass, ast.*, ast.builder, sema.{types,symbol}, strutil, std
compiler.sema.body_pass    ← ast.*, ast.builder, sema.*, lexer.token, strutil, std   (core)
compiler.sema.body_program ← sema.body_pass, sema.decl_{pass,collect}, ast.*, sema.*, std
compiler.codegen.c_codegen ← ast.*, lexer.token, std.list                            (core)
compiler.codegen.c_program ← codegen.c_codegen, ast.*, lexer.token, std.list
util.strutil               ← std.list   (chuỗi/số dùng chung: str_*, usz_to_str, cstr_to_str)
compiler.loader.loader     ← lexer.lexer, parser.parser, parser.decl, ast.*, util.strutil, std
main                        ← sema.{decl_pass,decl_collect,body_pass,body_program}, codegen.{c_codegen,c_program}, loader, util.strutil, std
```

Quy tắc: **`stdlib` là lá**; `ast`/`lexer` chỉ phụ thuộc `std`; `sema`/`codegen` phụ thuộc `ast` nhưng
**không** phụ thuộc lẫn nhau; `main` là nơi duy nhất ráp mọi thứ.

**Vì sao mỗi pass tách đúng 2 file.** Mỗi pass lớn (`decl_pass`, `body_pass`, `c_codegen`) trước đây là
một **SCC**: các hàm gọi đệ quy lẫn nhau qua lại (ví dụ `resolve_ast_type` ⟷ `instantiate_struct`, hay
`check_expr` ⟷ `check_statement`). Ngôn ngữ **cấm import vòng** (loader xếp module theo post-order, một
cạnh ngược sẽ khiến symbol của module kia chưa được nạp), nên không thể chẻ SCC theo chức năng
(collect/resolve/monomorph) một cách tuỳ ý. Cách chẻ đã dùng: **giữ `struct` + toàn bộ phần core/monomorph
ở module gốc, đưa các hàm *entry* (chỉ được gọi, không gọi ngược vào core) sang module mới**. Nhờ vậy đồ
thị vẫn một chiều: `decl_collect → decl_pass`, `body_program → body_pass`, `c_program → c_codegen`.

### 3.4 Đơn vị dữ liệu trung tâm

- **AST** (`src/compiler/ast`): `AstNode { kind, line, col, data: *u8 }`; `data` trỏ tới payload
  (`LiteralExpr`, `CallExpr`, `FnDecl`, …). Payload truy cập qua `as_*` (downcast), tạo qua `alloc_*`.
- **Kiểu sema** (`sema/types.kb`): `Type { kind, size, align, data }` với `data` trỏ tới payload
  (`PointerType`, `ArrayType`, `StructType`, `FnType`, `EnumInfo`). `DataType = VOID→NONE | … | STRUCT | FN`.
- **Bảng ký hiệu** (`sema/symbol.kb`): scope lồng nhau + module scope + import + registry enum + registry generic.
- **Arena**: mọi AST/kiểu cấp phát từ arena (`std.mem.arena`), giải phóng một lần.

---

## 4. Kiến trúc v0 (seed)

Pipeline: lexer → parser → `Analyzer` (pass0 index module, pass1 đăng ký khai báo, pass2 kiểm tra)
→ `CodeGen` (LLVM IR) → `opt` → native.

Điểm cần nhớ vì nó **ràng buộc v1**:

- v0 cũng phải hiểu mọi cú pháp/typing mà **nguồn của v1** dùng (hợp đồng bootstrap — §7).
- v0 tìm module ở: `./lib`, `./src/lib`, `./src`.
- Từ vựng kiểu do v0 nhận: `none` (không còn `void`), `i8..i64/isz`, `u8..u64/usz`, `f32/f64`,
  `bool`, `char`, `str`, `*T`, `&T`, `[T; N]`, `Struct`, `Enum`.

---

## 5. Thư viện chuẩn (`lib/std`)

Thuần Kobel, **không** phụ thuộc compiler; mọi extern đều qua `extern "libc"`.

| File | Nội dung |
|---|---|
| `io.kb` | `print/println`, `read_file/write_file`; handle stdio là `*none` (opaque `FILE*`) |
| `sys.kb` | `sys_exit`, `exec` |
| `ascii.kb` | `is_digit/is_alpha/to_lower/…` |
| `str.kb` | tầng thấp dựng `str` từ buffer byte (`str_from_bytes`); nền chung cho mọi formatter |
| `traits/to_str.kb` | trait `ToStr { fn to_str(val self): str }` + impl cho `bool/char/str`, số nguyên, `f32/f64`; nền của nội suy `${...}` |
| `traits/iterable.kb` | trait `Iterable { fn count(val self): usz; fn at(val self, i: usz) }` — kiểu impl nó thì `for (x in seq)` dùng `count()`/`at(i)` |
| `fmt.kb` | **umbrella** cho định dạng: `use std.traits.to_str.*` — một trait một tệp dưới `traits/` để thư mục lớn dần |
| `collections/list.kb` | `List<T>` (generic) — **field không `pub`**, dựng qua `List<T>()` / `List<T>(capacity)` (constructor inherent); impl `Iterable` nên `for (x in list)` chạy |
| `collections/hash_map.kb` | `HashMap<V>` — field riêng tư, dựng bằng `HashMap<V>()` (constructor inherent) |
| `collections/string_builder.kb` | `StringBuilder` — dựng bằng `StringBuilder()` (constructor inherent); impl `Iterable` nên `for (c in sb)` duyệt từng ký tự |
| `mem/alloc.kb` | **facade cấp phát** (chưa kiểm soát): byte `raw_alloc/raw_resize/raw_release`; typed `alloc<T>/alloc_array<T>/resize<T>/release<T>` |
| `mem/arena.kb` | `Arena` (vùng, giải phóng một lần) — field riêng tư, dựng bằng `Arena()` / `Arena(block_size)` (constructor inherent) + `arena_alloc<T>(&Arena): *T`; dùng `raw_*` của facade |

Quy ước `pub`: `List.cap`, mọi field của `Arena`/`ArenaBlock`, của `StringBuilder`/`HashMap` (trừ `len`
là accessor công khai), và các struct nội bộ (`StringRaw` ở `io.kb`/`string_builder.kb`, `StrRaw` ở
`str.kb`) **không** `pub` — module khác phải đi qua constructor/method. Riêng `List.data` / `List.len`
giữ `pub` vì hạ tầng `for` đọc trực tiếp (xem §6.2).

Toàn bộ các kiểu container (stdlib) và các pass/bộ nạp của compiler **chỉ** dựng bằng cú pháp constructor
Kotlin-style: `List<T>()`, `List<T>(capacity)`, `HashMap<V>()`, `StringBuilder()`, `Arena()`,
`Arena(block_size)`, `Lexer(src)`, `Parser(tokens)`, `ModuleLoader(dir, roots)`, `DeclPass()`,
`BodyPass()`, `SymbolTable()`, `CCodeGen()`.
Hàm khởi tạo được khai báo qua `pub fn new(...)` trong khối `impl Struct` (inherent constructor, **không**
dùng trait `New` để tránh trói buộc chữ ký arity), cấm tham số `self`. Toàn bộ hàm factory cũ kiểu C
(`new_*`) đã **gỡ bỏ hoàn toàn** — toàn bộ nguồn (compiler + std + examples) đã chuyển sang constructor,
không còn hai lối khởi tạo.

Quy ước ABI quan trọng:

- **`str` = `const char*`** (không phải fat-pointer). `.len` phải qua `strlen`.
- **`List<T>`** layout C: `{ T* data; size_t len; size_t cap; }`.
- **Cấp phát có kiểu**: hai lối vào, đều tự ép kiểu (không cần `as *T`):
  - **heap** — `alloc<T>()`, `alloc_array<T>(n)`, `resize<T>(p, n)`, `release<T>(p)` (`std.mem.alloc`).
  - **arena** — `arena_alloc<T>(&arena)` (`std.mem.arena`); vùng nhớ chết theo arena.
  - Mọi cấp phát thô (kể cả arena/list/string_builder/hash_map) đều đi qua facade `std.mem.alloc` (`raw_alloc`…).
- Thao tác chuỗi không dùng toán tử: dùng helper `kobel_*` do codegen chèn (§6.3).

---

## 6. Quy ước & bất biến (phần dễ vỡ nhất)

### 6.1 Sema là nơi quyết định, codegen **không suy luận kiểu**

Codegen C không có bảng ký hiệu. Vì vậy **mọi thứ cần kiểu phải được sema gắn sẵn vào AST**:

- Biến nội bộ không có annotation được sema **chèn `type_annotation`** (`ast_type_from_type`).
- Kiểu trả về của arrow-fn (`fn f() => expr`) được codegen suy ra qua `infer_type_from_expr`, và
  pass đăng ký hàm cũng dùng cùng logic (`fn_ret_c_type`) để prototype khớp định nghĩa.
- Tên kiểu trong AST được sema **đổi sang tên C đã mangle** ngay khi giải kiểu.

Hệ quả: codegen dùng heuristic theo tên (`pointer_vars`, `str_vars`) chỉ như bổ trợ; chỗ nào không
chắc thì sema phải biến đổi AST cho tường minh (ví dụ: truy cập member qua con trỏ không phải biến
được viết lại thành `(*p).field`).

### 6.2 Desugar trong sema (body_pass)

| Cú pháp nguồn | Hạ thành |
|---|---|
| `EnumType.MEMBER` | literal số nguyên |
| `enumvar.value` | chính biểu thức enum |
| `obj.method(args)` | `Struct_method(&obj, args)` (thêm `&` nếu `obj` là value); chọn **overload theo arity** |
| `Type(args)` | gọi overload `new` cùng arity nếu kiểu có (`Struct_new_<n>(args)`), ngược lại dựng theo field `(Struct){…}` |
| `Self(args)` trong `impl` | dựng **thô** theo field, **bỏ qua** bước gọi `new` ở trên |
| `a + b` với `a: str` | `kobel_concat(a, b)` |
| `a == b` / `a != b` với `str` | `kobel_streq(a, b)` / `!kobel_streq(a, b)` |
| `s.len` / `s.size` (`s: str`) | `kobel_slen(s)` |
| `s.slice(a, b)` | `kobel_slice(s, a, b)` (`b` mặc định `kobel_slen(s)`) |
| `T.size()` trong code generic | literal theo kích thước C |
| `arr.len` / `arr.size` (`arr: [T; N]`) | literal `usz` (kích thước biết lúc biên dịch) |
| `s.data` / `arr.data` (`str`, `[T; N]`) | chính biểu thức đó (con trỏ tới phần tử đầu) |
| `for (x in seq)` — `seq` là `str`/`[T; N]`/struct có `.len`+`.data` | `val __for_n = seq.len; var __for_i = 0; while (__for_i < __for_n) { val x = seq.data[__for_i]; …; __for_i += 1 }` |
| `for (x in seq)` — kiểu của `seq` (hoặc con trỏ `*T`/`&T`) có `impl Iterable` | `val __for_n = seq.count(); var __for_i = 0UZ; while (__for_i < __for_n) { val x = seq.at(__for_i); …; __for_i += 1 }` — phần tử lấy từ trait, container **không** cần là mảng phẳng; hỗ trợ cả container theo giá trị lẫn con trỏ |
| `for (i in a..b)` / `a..<b` / `a>..<b` | `while (__for_go) { … }` có cờ kết thúc; chiều tăng/giảm quyết định **lúc chạy** (`__for_up`). `..<` là dạng nửa mở (loại trừ biên cuối) — dùng cho idiom `for (i in 0..<seq.len)` |
| `"a${x}b"` | chuỗi `kobel_concat`; mỗi hố hạ thành `x.to_str()` (trait `ToStr` ở `std/traits/to_str.kb`) — riêng `str` giữ nguyên xi; hỗ trợ cả kiểu nguyên thuỷ, struct và con trỏ `*Struct`/`&Struct` impl `ToStr`. Kiểu không có `to_str` ⇒ lỗi biên dịch. **Không còn helper C nào cho nội suy** |

`for` có **hai đường**: nếu kiểu của đối tượng duyệt (hoặc kiểu con trỏ trỏ tới nó) có `impl Iterable`
thì đi qua trait (`count()`/`at(i)`, phần tử lấy kiểu từ `at`; các container danh sách định nghĩa của
compiler như `Program`, `ExternBlock`, `Scope`, `ModuleScope` đều đã `impl Iterable`); ngược lại dùng
`.len`/`.data` dựng sẵn (cho `str`, `[T; N]`, và struct nào có đúng hai field đó). `STMT_FOR` **không**
đi tới codegen: `body_pass` hạ nó thành block + `while` ngay khi kiểm tra, nên backend C không cần biết
gì về `for`. Bốn ghi chú ngữ nghĩa của pha 1:

- **Chỉ nhận lvalue** làm đối tượng duyệt (`x in self.items` được, `x in f()` không): thân vòng
  dùng lại biểu thức đó mỗi vòng, nên biểu thức tạm sẽ treo còn lời gọi hàm sẽ chạy lặp.
- **Độ dài chốt lúc vào vòng** (`__for_n`): vòng không giãn ra nếu thân vòng thêm phần tử; với
  `str` việc này còn bỏ được một lời gọi `strlen` mỗi vòng.
- **`continue` vẫn bước tiếp biến đếm**: `for_inject_step` chèn bước nhảy vào trước mọi `continue`
  thuộc vòng này (không đụng `continue` của vòng lồng bên trong).
- **Đếm an toàn tràn số**: bước nhảy chỉ chạy khi cờ "còn phần tử" còn đúng, nên biến đếm unsigned
  không bao giờ giảm xuống dưới biên.

Ba dạng range (`a..b` đóng, `a..<b` nửa mở, `a>..<b` mở hai đầu) đều lấy kiểu biến đếm từ biên
cuối, nên `for (i in 0..<n)` với `n: usz` duyệt unsigned và **không bao giờ tính `n - 1`**.

⚠️ Biến vòng là `val` **trong scope thân vòng**, nên code dựa vào biến đếm *sau* vòng mà chuyển sang
`for` sẽ **im lặng sai** (biến ngoài giữ nguyên giá trị cũ, không có lỗi biên dịch). Khi chuyển các
vòng `while` cầm tay sang `for`, phải soi riêng nhóm "biến đếm sống qua vòng".

### 6.3 Runtime helper trong C sinh ra

Prelude của file C chèn sẵn: `kobel_slice`, `kobel_concat`, `kobel_streq`, `kobel_slen`. **Không còn
helper chuyển-kiểu-sang-`str`**: mọi giá trị (kể cả `f32/f64`) đi qua trait `ToStr` viết thuần Kobel ở
`std/traits/to_str.kb`. Tất cả nhúng dưới dạng chuỗi trong `c_program.kb`; dự kiến tách ra
`codegen/runtime.c.inc`.

### 6.4 Hạ tầng C

| Khái niệm Kotel | C sinh ra |
|---|---|
| `struct S` | `typedef struct S S;` + `struct S { … };` |
| `enum E` | `typedef int32_t E;` (thành viên → hằng số) |
| `const X = …` cấp module | `#define X …` |
| mọi thứ khác (fn, method) | tên mangle `<module__>Name`, method `<StructC>_<method>`; method **overload** thêm hậu tố số đối số `<StructC>_<method>_<n>` (kèm `_<k>` nếu vẫn trùng) |
| `*T` (read-only) | `T*` — **trừ** `*char`/`*str` giữ `const` (nhận string literal / `.c_str()`) |
| `str` | `const char*` |
| `T.size()` | `sizeof(<tên C>)`; với tên kiểu **nguyên thuỷ** thì sema fold thành literal kích thước C (không có kiểu C tên `str`/`bool`) |
| `[T; N]` | `T*` — **không** phải mảng C inline, nên `[T; N]` không dùng được làm field struct (layout sema ≠ layout C) |
| `[a, b, c]` (array literal) | compound literal C99 `(T[]){a, b, c}`, decay thành con trỏ; phần tử `T` lấy từ annotation `[T; N]` nếu có, ngược lại suy từ phần tử đầu |

Thứ tự pass codegen: header + helper → `typedef` + gom `struct_names` → đăng ký return type hàm →
định nghĩa struct → prototype hàm → định nghĩa hàm/method.

### 6.5 Sema: các quyết định then chốt

- **Layout struct**: struct được cấp phát trước, field thêm sau ⇒ phải gọi `layout_struct()` để
  tính lại `offset/size/align`. Nếu quên, `size` = 0 (đã từng gây hỏng heap trong `List<T>`).
- **Import theo module**: `ImportBinding` có `owner_module`; `lookup` chỉ xét import của module hiện
  tại. (Trước đây import dùng chung toàn cục nên hai module trùng tên hàm lấn nhau.)
- **Enum**: symbol enum mang một *carrier type* (`kind` = kiểu cơ sở, `data` = `EnumInfo`) để vừa
  dùng được như số nguyên vừa nhận diện được `.value`.
- **Method**: `MethodInfo` gắn trực tiếp vào `StructType` (đi kèm `c_name`), nên gọi method dùng được
  **xuyên module** mà không cần import mangle.
- **Overload method**: một `impl` được phép có nhiều method **cùng tên** khác **số đối số**. Mỗi cái
  được mangle duy nhất (hậu tố arity), và gọi `obj.m(args)` chọn theo `args.len + 1` (kể cả receiver).
  **Chỉ theo arity** — cùng tên + cùng arity (khác kiểu tham số) là **lỗi "Ambiguous call"**, không
  chọn theo kiểu. Trait cũng nhận diện method theo `name + arity` (`TraitMethod.arity`).
- **Field `pub`**: `StructType` mang `module` + `has_private`; từng field mang `is_pub`. Một field
  không `pub` **chỉ** dùng được trong module khai báo — cưỡng chế ở **cả ba** đường: dựng bằng
  `Type(...)` (nếu struct có bất kỳ field riêng), **đọc** và **ghi** (`obj.field` / `obj.field = v`,
  đều đi qua nhánh `EXPR_MEMBER`). Module khác phải đi qua hàm khởi tạo/accessor. Cùng module thì
  không hạn chế (nên chính `new` dùng được).
- **`Self` & dựng kiểu Kotlin**: trong thân `impl`, `Self` = kiểu đang impl (`DeclPass.current_self_type` và
  `BodyPass.current_self_type`). Chữ ký `pub fn new(...): Self` trả về chính struct đang impl.
  Hàm khởi tạo `new` là constructor inherent (cấm tham số `self`); không thể gọi `Type.new(...)` hay `obj.new(...)`.
  `Self(args)` dựng **thô** theo field. Còn `Type(args)` **ưu tiên gọi overload `new`** cùng arity, chỉ
  khi không có mới dựng theo field. `Self(...)` **luôn bỏ qua** bước gọi `new` — nhờ vậy
  `fn new(v) => Self(v, v)` không tự gọi lại chính nó.
- **Monomorphization** (generic):
  - Pass 0 đăng ký *template* (`struct`/`fn`/`impl` có `<T>`), kèm `module`.
  - `List<X>` ở vị trí kiểu → instantiate; `f<X>(...)` / `Struct<X>(...)` → instantiate ở pass quét AST.
  - Instance sinh bằng **clone AST + thay type param**, tên C = `<mangle(template)>_<token(type arg)>`,
    trong đó token lấy từ **tên C đã mangle** của type arg (tránh trùng tên giữa các module).
  - Instance đăng ký vào `inst_syms` (tra cứu toàn cục) và được **chèn lên đầu** `program.declarations`
    để struct instance đứng trước struct dùng nó.
  - Chỉ hỗ trợ **type argument tường minh**; không suy luận type arg.
  - Một kiểu generic có thể có **nhiều `impl`** (impl inherent + impl trait); khi instantiate, **mọi**
    template cùng tên đều được nhân bản (`instantiate_struct` duyệt hết `impl_templates`), và
    `clone_impl` **giữ `trait_name`** nên hợp đồng trait được kiểm cả trên bản monomorphized.

### 6.6 Trait (tĩnh, monomorphized)

Trait trong Kobel là **dispatch tĩnh kiểu Rust** — không có vtable/`dyn`; mọi lời gọi method đều
phân giải về method của struct ngay ở sema.

- **Gom trait** (Pass 0, `decl_collect.collect_trait`): mọi `trait` vào `SymbolTable.traits` dưới dạng
  `TraitInfo { name, bases, methods, impls }`; mỗi `TraitMethod` chỉ giữ **tên** và **có thân hay
  không**. Kiểu chữ ký để phân giải muộn, nên không phụ thuộc thứ tự khai báo.
- **`impl Trait for Struct`** (`decl_pass.apply_trait`): đối chiếu hợp đồng —
  - Gộp method của trait **và toàn bộ base** (`trait_effective`): base duyệt trước để trait dẫn xuất
    ghi đè; danh sách `seen` chặn chu trình `trait A : B` / `trait B : A`.
  - Method **không có thân** là bắt buộc: impl thiếu ⇒ lỗi `does not implement required trait method`.
  - Method **có thân** mà impl bỏ trống ⇒ **nhân bản AST** (`clone_fn`) rồi thêm vào `im.methods`, nên
    backend phát nó y hệt method viết tay (không cần biết gì về trait).
- **Kế thừa**: `impl Derived for X` cũng đăng ký `X` vào `impls` của mọi base trait
  (`trait_record_impl`), nên bound `<T: Base>` được thoả khi `X: Derived` và `Derived : Base`.
- **Bound `<T: Trait>`** (`decl_pass.check_bounds`): kiểm ngay lúc **instantiate** generic
  (`instantiate_fn` / `instantiate_struct`). Chỉ báo lỗi khi type arg là **struct đã biết** mà chưa có
  `impl` tương ứng; type arg không phải struct (số, con trỏ, `str`…) được bỏ qua để tránh dương tính giả.
- Sau `collect_impl`, method trait trở thành method bình thường của struct ⇒ `self.m()`/`obj.m()` do
  `body_pass` hạ như mọi method khác (`Struct_m(&obj, …)`); codegen không biết trait là gì.

**Method cho kiểu nguyên thuỷ** (`impl i32 { … }`, `impl str { … }`): primitive không phải struct và
`make_primitive` trả `Type` **mới mỗi lần gọi** (không phải singleton), nên method không gắn vào `Type`
được. Thay vào đó:
- `collect_impl` nhận ra tên kiểu qua `resolve_primitive_name` và gọi `collect_prim_impl`: điền kiểu
  tường minh cho tham số `self` (để backend truyền **theo giá trị**, vd `int32_t self`) rồi ghi method
  vào bảng `SymbolTable.prim_methods` (khoá theo `DataType`), tên C = `<prim>_<method>` (`i32_to_str`).
- `body_pass` tra `find_prim_method(kind, name)` khi receiver là kiểu nguyên thuỷ ⇒ hạ `x.m(args)` thành
  `<prim>_m(x, args)` (receiver theo giá trị, tự `*` nếu receiver là con trỏ).
- `check_impl` nhận diện impl nguyên thuỷ qua `struct_name == ""` và kiểm body bằng `find_prim_method_c`.

Nền tảng này nuôi **trait `ToStr`** (`lib/std/traits/to_str.kb`, gom qua umbrella `lib/std/fmt.kb`).
Driver (`src/main.kb`) **luôn nạp `std.fmt`** (nếu tìm thấy) — kéo theo `std.traits.to_str` — để impl
`ToStr` cho `bool/char/str/số nguyên` luôn tồn tại, bất kể input có `use` hay không; nhờ vậy nội suy
`${x}` hạ được thành `x.to_str()`.

⚠ Hạn chế: chưa có `dyn`/trait object; hợp đồng so theo **tên method + arity** (chưa so kiểu chữ ký);
method nguyên thuỷ dùng tên C toàn cục (`i32_to_str`) nên hai module cùng impl một method cho một kiểu
sẽ đụng tên; hợp đồng của `impl Trait for Generic<T>` chỉ được kiểm **khi instantiate**, không kiểm trên
template. Thứ tự các `impl` trong tệp **không** còn quan trọng.
Riêng `ToStr` cho `f32/f64` dùng printer **fixed-point** (6 chữ số thập phân, bỏ số 0 cuối) — **không**
có ký pháp mũ, nên giá trị quá lớn/quá nhỏ mất chính xác.

---

## 7. Hợp đồng bootstrap & fixpoint

**Hợp đồng**: `src/compiler` chỉ được dùng cú pháp/typing mà `seed` hiểu. Khi thêm cú pháp mới, phải
hoặc (a) thêm vào seed, hoặc (b) chấp nhận rằng bản seed cũ không build được nguồn mới.

**Fixpoint**: hai lần biên dịch liên tiếp cho **cùng output**:

```
kobel_v1.exe src/main.kb -emit-c a.c
kobel_v2.exe src/main.kb -emit-c b.c     # v2 dựng từ a.c
sha256(a.c) == sha256(b.c)
```

Đây là tiêu chí "self-host đạt" (đã đạt). Dự kiến mã hoá thành task `fixpoint` trong `tools/bootstrap.ps1`.

Ngoài ra còn một mức mạnh hơn: `kobel_v2.exe` == `kobel_v3.exe` về hành vi đầu ra.

---

## 8. Build & test

### 8.1 Hiện tại

| Việc | Lệnh |
|---|---|
| Build seed (từ C) | `scripts/build_seed.bat` → `build/seed/kobel_seed.exe` (biên dịch `dist/bootstrap.c`) |
| Build v1 | `scripts/build_bootstrap.bat` → `kobel_v1.exe` (seed biên dịch `src/main.kb`) |
| Test v1 | `scripts/test_all_bootstrap.bat` (13 test, gồm `examples/std_demo.kb`) |
| Chạy 1 chương trình | `scripts/run_test.bat <file.kb>` |
| Regenerate seed | `kobel_v1.exe src/main.kb -emit-c dist/bootstrap.c` |

v0 (C++/LLVM) nằm ở nhánh **`v0-cpp`**: build bằng `cmake --build cmake-build-debug` (CMake + vcpkg/LLVM
env), test bằng `test/test_*.cpp` (7 bộ). Master (nhánh này) không còn mã C++.

⚠ Các test **có bước link** phải chạy trong môi trường MSVC (`vcvars64`), nếu không sẽ báo thiếu
`libcmt.lib`. Chạy từ Developer Prompt hoặc gọi qua wrapper.

### 8.2 Vấn đề cần dọn

- Artifact (`kobel_v1/v2/v3.exe`, `out.c`, `*.tmp.obj`) rơi vào **gốc repo** (đã gitignore; nên gom vào `build/`).
- Test của v1 nằm lẫn trong `examples/` (`test_bootstrap_*.kb`); danh sách test **hardcode** trong `.bat`.
- Mỗi `.bat` tự dò `vcvars`; ~~`scripts/tmp_diag.bat`~~ (đã xoá).
- Không có script `clean`/`fixpoint`.

---

## 9. Bố cục đích & lộ trình tái cấu trúc

> **Trạng thái (chốt gần nhất):** lần tái cấu trúc hiện tại **giữ** bố cục đang dùng —
> `src/main.kb` + `src/compiler/**`, `lib/std`, `examples`, `scripts` — và module prefix `compiler.`.
> Bố cục `compiler/self` + `kobel.` bên dưới là **phương án dự phòng**, chưa thực thi.

### 9.1 Bố cục đích

```
kobel/
├─ README.md
├─ docs/{spec.md, architecture.md, bootstrap.md}
├─ compiler/
│  ├─ self/              ← src/bootstrap  (compiler thật)
│  │  ├─ main.kb
│  │  ├─ lexer/ parser/ ast/ sema/ codegen/ driver/ support/
│  │  └─ codegen/runtime.c.inc
│  └─ seed/              ← src/ + CMakeLists.txt + test/*.cpp (v0, đóng băng)
├─ stdlib/               ← lib/std
│  ├─ {io,sys,ascii}.kb  collections/ mem/
├─ tests/
│  ├─ self/              ← examples/test_bootstrap_*.kb
│  ├─ e2e/               ← examples/*_demo.kb + fixtures
│  └─ seed/              ← test/*.cpp
├─ tools/{bootstrap.ps1, msvc.ps1}
└─ build/                (gitignore: exe, .c, .obj, cmake-build)
```

Đổi kèm: module prefix `bootstrap.` → `kobel.`; loader root `lib` → `stdlib` (sửa ở **cả** loader v0
và v1).

### 9.2 Các pha

| Pha | Việc | Rủi ro |
|---|---|---|
| 0 | dọn rác: gitignore `/build/`, xoá `kobel_v*.exe`/`out.c`/`*.tmp.obj`/`tmp_diag.bat`, `git mv dac-ta-ngon-ngu.md docs/spec.md` | thấp |
| 1 | `git mv` theo §9.1 (giữ lịch sử) + cập nhật path trong CMake, `.bat`, loader roots | thấp–vừa |
| 2 | gộp build vào `tools/bootstrap.ps1` (`build\|self\|fixpoint\|test\|clean`) + `tools/msvc.ps1`; test tự khám phá | thấp |
| 3 | chẻ file phình — **đã làm** theo ranh giới không-vòng (§3.3): `decl_pass`→`+decl_collect`, `body_pass`→`+body_program`, `c_codegen`→`+c_program`. Phần core/monomorph giữ nguyên ở module gốc vì là SCC. `runtime.c.inc`: chưa tách (prelude C vẫn trong `c_program`) | vừa |
| 4 | **bỏ v0/LLVM**: commit `dist/bootstrap.c`, seed = C compiler | vừa (nhưng xoá ~12 700 LOC) |

Sau mỗi pha: build v1 → tự biên dịch → `fixpoint` → 8/8 test.

### 9.3 Đích cuối

`dist/bootstrap.c` (~8.9k dòng) là seed; build bằng bất kỳ C compiler nào. Khi đó:

- Bỏ `compiler/seed` (C++/LLVM) + `test/*.cpp` + phụ thuộc LLVM/vcpkg/zstd.
- Repo còn ~8 900 LOC (Kobel + std) + 1 file C seed.
- Tính năng mới chỉ viết **một lần** ở `compiler/self`; vòng lặp phát triển là: sửa `.kb` → build bằng
  seed C → `fixpoint` tự kiểm.

---

## 10. Hạn chế đã biết (TODO kiến trúc)

- **Generics**: chỉ type arg tường minh; generic impl phải cùng tên struct template; `T.size()` hạ
  thành literal theo layout của v1 (khớp thực tế cho các kiểu đang dùng). **Bound `<T: Trait>` đã có**
  (xem §6.6), nhưng **generic impl của trait** (`impl Trait for List<T>`) chưa được hỗ trợ.
- **Trait**: dispatch **tĩnh** (không vtable/`dyn`); hợp đồng so theo **tên method + arity** (chưa so
  kiểu chữ ký). Đã áp được cho **kiểu nguyên thuỷ** (`impl i32 { … }`). Kiểu container dùng
  constructor inherent `new(...)` trong khối `impl Struct` (không cần trait `New`).
- **Dựng giá trị**: `Type(args)` ưu tiên `new` overload; `Self(...)` dựng thô; struct có field không
  `pub` thì module khác **không** dựng trực tiếp được (phải qua `new`).
- **Nội suy chuỗi** phụ thuộc `std.fmt` (→ `std.traits.to_str`): driver nạp ngầm module này; nếu không
  tìm thấy `lib/std/fmt.kb`, nội suy hố khác `str` sẽ báo lỗi biên dịch. **Literal số thực** (`1.5`)
  đã có; `ToStr` cho `f32/f64` dùng printer fixed-point (không có ký pháp mũ).
- **`for`**: chỉ duyệt lvalue; trait `Iterable` đã có nên container **không** cần mảng phẳng —
  `List<T>` (kể cả phần tử là struct) và `StringBuilder` (theo ký tự) đã impl. `str` / `[T; N]` đi
  đường `.len`/`.data` dựng sẵn (mảng cố định hiện **chưa tạo được giá trị**: không có array literal và
  `TYPE_ARRAY` emit thành `T*`, xem mục array literal ở trên). `HashMap` **chưa** impl `Iterable` (kho
  lưu thưa ⇒ cần cursor, hoặc `for ((k, v) in map)` cần destructuring). Dạng nửa mở `a..<b` đã được
  áp dụng toàn diện thay thế toàn bộ các vòng `while (i < n)` đếm index thủ công trong compiler (`strutil`,
  `c_codegen`, `parser`), stdlib (`to_str`, `io`, `string_builder`, `hash_map`) và test suite; các vòng
  `while` còn lại đều là nhóm đặc thù có lý do giữ lại (bước nhảy biến động phân tích CLI, tiêu thụ token
  stream động trong lexer/parser, hoặc nhân đôi dung lượng theo cấp số nhân).
- **Generic method** (`impl S { fn f<T>() }`) **không** được hỗ trợ: tham số `T` rò nguyên vào C
  (`error C2065: 'T' undeclared`). Vì vậy cấp phát có kiểu phải là **free generic function**
  (`alloc<T>(&arena)`), không thể là method `arena.alloc<T>()`.
- **`none`**: đã thay `void` ở cả v0/v1; `void` giờ là lỗi biên dịch.
- **Kiểu hàm bậc nhất** (`fn` type) chưa dùng trong `ast_type_from_type` (trả `null` → codegen tự suy).
- **Mảng cố định**: `val a: [T; N] = [x, y, z]` đã dùng được (sema định kiểu bằng phần tử đầu, codegen
  phát compound literal `(T[]){…}`). Còn hạn chế:
  - `[T; N]` được biểu diễn bằng **con trỏ**, storage của literal là **tự động** (compound literal) —
    không được trả về từ hàm hay giữ quá scope của nó;
  - literal **rỗng** `[]` là lỗi (không suy được kiểu phần tử);
  - khi không có annotation, kiểu phần tử do codegen suy từ **phần tử đầu**; nếu phần tử là biến
    không thuộc dạng literal/call biết kiểu, phải ghi annotation `[T; N]`;
  - `[T; N]` **không** dùng được làm field struct (sema tính `N × sizeof(T)` còn C phát con trỏ).
- **Mảng cố định không có trait**: `[T; N]` không phải struct nên không viết được `impl Iterable for
  [T; N]`; `for` xử lý mảng bằng đường `.len`/`.data` dựng sẵn (xem §6.2). Muốn "list-like" dùng chung
  trong code generic thì cần thêm associated type (hoặc generic method) cho trait — hiện chưa có.
- **`.size()` trên tên kiểu nguyên thuỷ** (`str.size()`, `bool.size()`) được sema **fold thành literal
  theo kích thước C** (`prim_c_size` trong `body_pass`), vì backend không có kiểu C tên `str`/`bool`.
  `str` tính là con trỏ (8 byte), khớp `ast_type_size` dùng cho đường generic.
- Cảnh báo C khi build ở `/Wall`: còn `C5045` (Spectre note), `C4820` (padding) — mang tính thông tin.
- Hai bản cài đặt sema (v0 và v1) song song ⇒ nguy cơ lệch hành vi; giảm dần bằng cách đóng băng seed.
