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
| 3 | `check_program(body_program, prog)` rồi `lower_program(lower, prog)` | kiểm tra thân hàm, gắn kiểu cho biến nội bộ (core ở `body_pass`), **instantiate generic method** khi đã biết kiểu receiver rồi flush instance mới; sau đó một pass **lower** riêng hạ cấp các dạng đường (xem §6.2) |
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
compiler.sema.decl_pass    ← ast.*, ast.builder, sema.{types,symbol}, strutil, std   (core)
compiler.sema.monomorphize ← sema.{types,symbol,decl_pass}, ast.*, std   (template tables, substitution, worklist)
compiler.sema.mono_instantiate ← monomorphize (instantiate_*, collect_*_as)
compiler.sema.mono_clone   ← monomorphize (clone_* : AST + type-parameter substitution)
compiler.sema.mono_rewrite ← monomorphize (rewrite_generics: calls, type nodes, T.size())
compiler.sema.decl_collect ← sema.{decl_pass,monomorphize,mono_*}, ast.*, ast.builder, sema.{types,symbol}, strutil, std
compiler.sema.body_pass    ← ast.*, ast.builder, sema.{types,symbol,decl_pass,desugar_*}, lexer.token, std  (core + dispatcher)
compiler.sema.body_expr    ← body_pass, primaries & operator families
compiler.sema.body_access  ← body_pass, member/index, lvalue, assignment, indexed write
compiler.sema.body_pattern ← body_pass, when/is patterns
compiler.sema.body_interp  ← body_pass, interpolation holes
compiler.sema.body_call    ← body_pass, body_expr (call resolution, ctors)
compiler.sema.body_stmt    ← body_pass, desugar_for (statements, `for` driver)
compiler.sema.body_program ← body_pass, decl_{pass,collect} (function/program drivers)
compiler.sema.lower        ← body_pass, ast.*, sema.{types,desugar_interp}   (rewrites sugar for C)
compiler.codegen.c_type    ← ast.types, sema.types
compiler.codegen.c_codegen ← ast.*, sema.types, c_type (state, literals, type helpers)
compiler.codegen.c_expr    ← c_codegen, c_type (expression emission)
compiler.codegen.c_stmt    ← c_codegen, c_type (statement emission)
compiler.codegen.c_program ← codegen.{c_codegen,c_expr,c_stmt,c_type}, ast.*, lexer.token, std.list
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

- **AST** (`src/compiler/ast`): `AstNode { kind, line, col, data: NodeData, type_ptr: *u8 }`.
  `NodeData` là **payload enum**, mỗi variant giữ `*Payload` (`LiteralExpr`, `CallExpr`, `FnDecl`, …);
  truy cập qua accessor `node.as_<shape>()` (pattern match), tạo node qua
  `arena.alloc_node(kind, NodeData.<Shape>(payload), line, col)` và các builder 1-liner trên `Arena`.
  Payload struct được **khai báo trước** trong `ast.node`, định nghĩa ở `ast.types/expr/stmt/decl`
  (forward declaration xuyên module), nên đồ thị module vẫn một chiều. `type_ptr` là `*Type` đã giải
  của **biểu thức** (xem §6.1), để codegen không phải suy luận.
- **Kiểu sema** (`sema/types.kb`): `Type { kind, size, align, data: TypePayload, methods: *PrimMethods }`.
  `TypePayload` là **payload enum** (`NONE | Pointer | Array | Struct | Fn | Enum`) chứa con trỏ tới
  payload tương ứng (`PointerType`, `ArrayType`, `StructType`, `FnType`, `EnumInfo`); accessor
  `as_pointer/as_array/as_struct*/as_fn*/as_enum` là pattern match có kiểm tra (sai kind → null), hết
  `data as *X`. `methods` trỏ tới payload `PrimMethods` cho method của kiểu nguyên thuỷ.
  `DataType = VOID→NONE | … | STRUCT | FN`. `PrimTypes` giữ **instance chuẩn** cho 16 kiểu nguyên thuỷ
  (một con trỏ cho mỗi kiểu trong toàn bộ lần biên dịch).
- **Bảng ký hiệu** (`sema/symbol.kb`): scope lồng nhau + module scope + import + registry enum + registry generic.
- **Arena**: mọi AST/kiểu cấp phát từ arena (`std.mem.arena`), giải phóng một lần.

### 3.5 Import vòng

Loader đã chấp nhận chu trình (đánh dấu module trước khi duyệt `use`, nên cạnh quay lại bị bỏ qua) và
`ordered` là post-order nên phụ thuộc luôn đứng trước. Vấn đề cũ nằm ở DeclPass: nó giải field type ngay
khi gặp struct, nên struct ở module A không thấy kiểu của module B nếu B import ngược lại A. Hai pass sớm
sửa việc đó:

- **Pass 0.4**: đăng ký **wildcard import** trước khi bất cứ tên nào được giải (explicit import vẫn chờ
  pass 1a vì cần symbol đích). Việc đăng ký là idempotent nên pass 1a chạy lại vô hại.
- **Pass 0.5**: mỗi `struct` không generic được **khai báo trước** dưới dạng shell (tên + type rỗng,
  `is_defined = false`); pass định nghĩa điền vào shell qua đúng nhánh merge của forward declaration.

Nhờ vậy con trỏ chéo module hoạt động (`struct A { b: *B }` ↔ `struct B { a: *A }`, test
`test_loader_cycles.kb`). Giới hạn: tham chiếu chéo **theo giá trị** (hoặc `[B; N]`) vẫn sai vì shell
chưa có size tại lúc giải — C cũng không cho struct chứa chính nó theo giá trị, nên đây là giới hạn tự
nhiên chứ không phải thiếu sót cần vá.

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
| `mem/alloc.kb` | **facade cấp phát** (chưa kiểm soát): typed `alloc<T>/alloc_array<T>/resize<T>/release<T>` gọi libc trực tiếp |
| `mem/arena.kb` | `Arena` (vùng, giải phóng một lần) — field riêng tư, dựng bằng `Arena()` / `Arena(block_size)` (constructor inherent) + `arena_alloc<T>(&Arena): *T`; dùng facade `std.mem.alloc` |

Quy ước `pub`: `List.cap`, mọi field của `Arena`/`ArenaBlock`, của `StringBuilder`/`HashMap` (trừ `len`
là accessor công khai), và struct nội bộ `StrRaw` (ở `str.kb`) **không** `pub` — module khác phải đi qua
constructor/method. Riêng `List.data` / `List.len`
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
  - Mọi cấp phát bộ nhớ (arena/list/string_builder/hash_map...) đều đi qua generic facade `std.mem.alloc` (`alloc<T>`, `alloc_array<T>`...).
- Thao tác chuỗi không dùng toán tử: dùng helper `kobel_*` do codegen chèn (§6.3).

---

## 6. Quy ước & bất biến (phần dễ vỡ nhất)

### 6.1 Sema là nơi quyết định, codegen **không suy luận kiểu**

Codegen C không có bảng ký hiệu. Vì vậy **mọi thứ cần kiểu phải được sema gắn sẵn vào AST**:

- Mỗi biểu thức đã kiểm tra mang kiểu đã giải trong `AstNode.type_ptr` (`*Type`, khai báo `*u8` để module
  `ast` vẫn là lá). `check_expr` gắn kiểu **sau khi** node đã bị hạ cấp, nên annotation luôn mô tả đúng
  node mà codegen nhìn thấy. Codegen ánh xạ sang C bằng `c_type_from_type` (module
  `codegen/c_type.kb`); **không** còn hàm suy luận kiểu nào trong backend. Pass `lower` cũng đọc chính
  kênh này khi cần `size`, field struct hay bảng method.
- Quyết định **phân giải** (method/operator nào được chọn) được ghi vào `AstNode.resolved_method`
  (`*MethodInfo`, cũng là `*u8`). Nhờ vậy `lower` dựng lời gọi C mà không tra lại overload/trait —
  và không có hai bản logic phân giải có thể lệch nhau.
- Biến nội bộ không có annotation vẫn được sema **chèn `type_annotation`** (`ast_type_from_type`).
- Kiểu trả về của arrow-fn (`fn f() => expr`) do **pass suy luận riêng** (`infer_arrow_returns`) tính
  trước khi kiểm thân hàm: nó kiểm biểu thức thân trong scope riêng, lặp tới fixpoint khi gặp arrow fn
  khác chưa có kiểu, rồi ghi vào `FnType.return_type` và `FnDecl.return_type`.
  `fn_ret_c_type` chỉ đọc lại, không suy luận.
- Tên kiểu trong AST được sema **đổi sang tên C đã mangle** ngay khi giải kiểu.

Hệ quả: codegen **không còn heuristic theo tên** nào. `Point(...)` là node `StructCtorExpr` do sema tạo;
typedef struct do `StructDecl.emit_typedef` quyết định (không còn tập tên); tag của pattern matching là
**field đầu tiên** của struct (quy tắc ngôn ngữ, không dò tên).

Bất biến đi kèm (đã có test C snapshot bảo vệ, §8.1): **thay đổi sema/codegen không được đổi C sinh ra
cho corpus test**, trừ khi đó là chủ đích và golden được cập nhật trong cùng commit.

### 6.2 Desugar trong sema (body_pass + lower)

Từ P2, sema tách làm hai việc: `check_*` **chỉ** quyết định kiểu và báo lỗi; `sema/lower.kb` **viết lại**
cây sau khi mọi thân hàm đã được kiểm. Bảng dưới ghi nơi hạ cấp từng dạng; cột "Lower" là các dạng đã
chuyển sang pass riêng, phần còn lại vẫn hạ ngay trong `check_*` (`replace_node`).

| Cú pháp nguồn | Hạ thành |
|---|---|
| `EnumType.MEMBER` | literal số nguyên |
| `enumvar.value` | chính biểu thức enum |
| `obj.method(args)` | `Struct_method(&obj, args)` (thêm `&` nếu `obj` là value); chọn **overload theo arity** |
| `Type(args)` | gọi overload `new` cùng arity nếu kiểu có (`Struct_new_<n>(args)`), ngược lại dựng theo field `(Struct){…}` |
| `Self(args)` trong `impl` | dựng **thô** theo field, **bỏ qua** bước gọi `new` ở trên |
| `a + b` với `a: str` | `kobel_concat(a, b)` |
| `a += b` với `a: str` | `a = kobel_concat(a, b)` |
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
  - Mỗi instance là một cặp `module marker, decl` trong **một** danh sách `instances`, theo thứ tự
    tạo (dependency luôn đứng trước instance cần nó). Danh sách này vừa là **worklist**: sau pass quét
    khai báo, `drain_instances()` rewrite từng instance trong module của template, lặp tới fixpoint, để
    generic use nào clone bỏ sót cũng được instantiate trước khi kiểm thân hàm. Cuối cùng danh sách
    được **đặt trước** `program.declarations` để struct instance đứng trước struct dùng nó.
  - Chỉ hỗ trợ **type argument tường minh**; không suy luận type arg.
  - **Generic method** (`x.m<T>(...)`) phân giải ở **body pass**, nơi biết kiểu receiver: template
    ghi `self_type` (kiểu impl) nên hai struct cùng module có thể trùng tên method generic; instance
    được đặt tên kèm kiểu receiver (`Arena_alloc_i32`). Clone tạo sau pass khai báo được **flush** ở
    cuối body pass (kiểm thân + phát khai báo trước decl gốc), và clone nhớ module + `Self` của
    template. `lower` chỉ phát node thay thế qua `resolved_node`; constructor `new` được ghi vào
    `resolved_method` để kiểm lại một node là idempotent.
  - Một kiểu generic có thể có **nhiều `impl`** (impl inherent + impl trait); khi instantiate, **mọi**
    template cùng tên đều được nhân bản (`instantiate_struct` duyệt hết `impl_templates`), và
    `clone_impl` **giữ `trait_name`** nên hợp đồng trait được kiểm cả trên bản monomorphized.

**Payload enum (P‑1).** Variant mang field có tên (`Circle(radius: f64)`). Sema hạ enum thành **một
struct**: field `tag: i32` + (về mặt layout) union các payload struct; mỗi variant có struct type
riêng tên `Enum.Variant` (C name `Enum__Variant`) và symbol hằng `Enum.Variant__TAG`. Nhờ vậy toàn bộ
máy `is` sẵn có dùng lại được: `is Shape.Circle as c` chỉ cần thêm `payload_path` (`u.Circle`) để
backend bind `&subject.u.Circle`; dựng giá trị (`Shape.Circle(...)`, `Shape.Empty`) hạ thành
`EnumCtorExpr` (sema tạo, `lower` đi vào args, codegen phát compound literal). Enum **không payload**
giữ nguyên: kiểu nguyên thuỷ + `.value` (integer literal). Phía C: phát struct variant, struct
`{ int32_t tag; union {...} u; }`, typedef và macro tag.

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

**Associated type** (`type Item;` / `type Item = T;`). `TraitInfo.assoc_types` giữ danh sách tên trait
khai báo; `ImplDecl.assoc_bindings` giữ binding của impl. `register_assoc_bindings` (gọi ở đầu
`collect_impl`, **trước** `apply_trait` và trước khi collect method) kiểm hợp đồng (đủ, không lạ, không
trùng), giải kiểu binding với `current_self_type` = struct đang impl rồi lưu vào
`SymbolTable.assoc_types` theo khóa `<struct>.<assoc>` — nên body pass (dùng lại `DeclPass`) cũng tra
được. `resolve_ast_type` nhận tên `Self.Item`: trả kiểu đã bind và **đổi tên AST annotation** sang tên C
của kiểu đó (vì `gen_fn_decl`/var decl đọc AST, không đọc `Type`). Hệ quả: binding phải là kiểu có tên
(struct/enum/primitive), không phải con trỏ/mảng. Impl generic clone binding kèm substitution
(`clone_impl`), nên mỗi instance có binding riêng; method mặc định của trait dùng `Self.Item` cũng chạy
vì chúng được clone vào impl sau khi binding đã đăng ký.

**Method cho kiểu nguyên thuỷ** (`impl i32 { … }`, `impl str { … }`): primitive không phải struct.
Từ P3 mỗi kiểu nguyên thuỷ có **một instance chuẩn** (`PrimTypes`, giữ trong arena của `SymbolTable`),
nên `i32` là cùng một con trỏ ở mọi nơi; method của nó nằm **trên chính `Type`** (payload `PrimMethods`)
chứ không còn bảng phụ:
- `collect_impl` nhận ra tên kiểu qua `resolve_primitive_name` (trả instance chuẩn) và gọi
  `collect_prim_impl`: điền kiểu tường minh cho tham số `self` (để backend truyền **theo giá trị**,
  vd `int32_t self`) rồi `prim.add_method(mi)`, tên C = `<prim>_<method>` (`i32_to_str`).
- `body_pass` tra `find_prim_method(kind, name)` (helper mỏng trên `SymbolTable`, đọc instance chuẩn)
  khi receiver là kiểu nguyên thuỷ ⇒ hạ `x.m(args)` thành `<prim>_m(x, args)` (receiver theo giá trị,
  tự `*` nếu receiver là con trỏ).
- `check_impl` nhận diện impl nguyên thuỷ qua `struct_name == ""` và kiểm body bằng `find_prim_method_c`.

`Type` khai báo **trước** `MethodInfo`, còn danh sách method của nó cần `MethodInfo`, nên `PrimMethods`
được **khai báo trước** (`struct PrimMethods;`, P‑1) và định nghĩa sau `MethodInfo`; `Type.methods` là
`*PrimMethods`. Trước khi có khai báo trước, chỗ này buộc phải là `*u8` + cast.

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

Đây là tiêu chí "self-host đạt" (đã đạt) và nay kiểm tự động bằng `kobel fixpoint`.

⚠ Fixpoint **chỉ** chứng minh tính ổn định, không chứng minh tính đúng: một lỗi lặp lại ở cả hai thế hệ
vẫn cho fixpoint xanh. Vì vậy cổng này phải đi kèm `kobel test` (hành vi) và `kobel snap` (C sinh ra) —
xem §8.1.

Ngoài ra còn một mức mạnh hơn: `kobel_v2.exe` == `kobel_v3.exe` về hành vi đầu ra.

---

## 8. Build & test

### 8.1 Hiện tại

| Việc | Lệnh |
|---|---|
| Build seed (từ C) | `scripts/build_seed.bat` → `build/seed/kobel_seed.exe` (biên dịch `dist/bootstrap.c`) |
| Build compiler | `build/kobel.exe build` (tự động theo `kobel.toml`, xuất `build/kobel.exe`) |
| Run project | `build/kobel.exe run [args...]` (tự động kiểm tra thay đổi mã nguồn trước khi chạy) |
| Test suite | `build/kobel.exe test [filter]` (chạy 17 bài kiểm tra chuẩn hóa với bộ lọc linh hoạt) |
| C snapshot | `build/kobel.exe snap` (so C sinh ra với `tests/golden/`); `snap --update` để chấp nhận thay đổi |
| Fixpoint | `build/kobel.exe fixpoint` (build thế hệ 2 từ C của chính nó rồi so byte) |
| Regenerate seed | `build/kobel.exe src/main.kb -emit-c dist/bootstrap.c` |

**Cổng kiểm tra trước khi commit** (cả ba phải xanh): `kobel build` → `kobel test` → `kobel snap` →
`kobel fixpoint`. `snap` là cổng bắt thay đổi ngữ nghĩa mà test hành vi bỏ sót; `fixpoint` là cổng bắt
thay đổi làm mất tính self-host. Khi `snap` báo lệch, phải khẳng định thay đổi là chủ đích rồi mới
`snap --update` và commit `tests/golden/` **cùng** commit mã nguồn.

Mỗi golden là một digest (`hash_stamp` của toàn bộ C sinh ra), không phải bản C đầy đủ: đủ để phát hiện
lệch, gọn để review. Corpus gồm toàn bộ `tests/**/test_*.kb` cộng `src/main.kb` (chính compiler).

Tất cả các tệp sinh ra khi dựng và kiểm thử (`.exe`, `.obj`, `.tmp.*`, `.kobel_stamp`) đều được cô lập bên trong thư mục `build/`.

Trình biên dịch tự động dò tìm công cụ MSVC qua `vswhere` khi cần, không còn sử dụng bất kỳ đường dẫn cứng nào.

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

Sau mỗi pha: build v1 → tự biên dịch → `test` + `snap` + `fixpoint` (xem §8.1).

### 9.3 Đích cuối

`dist/bootstrap.c` (~8.9k dòng) là seed; build bằng bất kỳ C compiler nào. Khi đó:

- Bỏ `compiler/seed` (C++/LLVM) + `test/*.cpp` + phụ thuộc LLVM/vcpkg/zstd.
- Repo còn ~8 900 LOC (Kobel + std) + 1 file C seed.
- Tính năng mới chỉ viết **một lần** ở `compiler/self`; vòng lặp phát triển là: sửa `.kb` → build bằng
  seed C → `fixpoint` tự kiểm.

### 9.4 Tái cấu trúc ngữ nghĩa (typed IR) — trạng thái

Mục tiêu không phải "chia lại file" mà là đạt ba bất biến:

- **I1.** AST bất biến sau parse; mọi hạ cấp sinh node mới (không `replace_node`).
- **I2.** Codegen **không** suy luận kiểu; mọi quyết định ngữ nghĩa được vật chất hoá thành dữ liệu có kiểu.
- **I3.** Pass một chiều, thuần: `resolve → check → lower → codegen`; không pass nào gọi ngược pass khác
  (hiện `BodyPass.resolve_type` vẫn tạo `DeclPass` — vi phạm I3, sẽ xử lý ở P3).

| Pha | Nội dung | Trạng thái |
|---|---|---|
| **P0** | Lưới an toàn: `kobel snap` (golden C) + `kobel fixpoint` (§8.1) | **xong** (`bd99dbd`, `8ce626c`) |
| **P1** | `AstNode.resolved_type` + codegen đọc kiểu thay vì đoán; xoá `infer_type_from_expr`, `pointer_vars`, `str_vars`, map return-type theo tên | **xong** (`f2c0043`, `16a864a`) |
| **P2** | Tách `check` (thuần, không sửa AST) khỏi `lower` (sinh node mới); xoá `replace_node`; bỏ hack `check_arrow_method_body` | **xong** (`415fc4e`, `d68221f`, `c6588f8`): `replace_node` và mọi phép gán `kind`/`data` đã bị xoá; `for` đi qua `resolved_node`; `check_arrow_method_body` đã bị xoá, thay bằng pass `infer_arrow_returns` (fixpoint trên đồ thị gọi, rollback chẩn đoán khi bị chặn). Ba kênh `type_ptr`, `resolved_method`, `resolved_node` là nơi duy nhất codegen/lower đọc quyết định của check |
| **P3** | `TypeStore` chuẩn hoá (primitive singleton, intern, struct id) + một `TypeResolver` dùng chung cho `DeclPass`/`BodyPass`; bỏ `DummyTemplateDecl` (type-punning) | **xong** (`4afe848`, `de7b2eb`, `41ee0a9`, `b6cb3a3`, `5200a18`): primitive canonical (`PrimTypes`), `DummyTemplateDecl` xoá, `BodyPass` dùng lại `DeclPass` (gỡ vi phạm I3), một kênh kiểu duy nhất `*Type` + `c_type_from_type`, method của primitive nằm trên `Type` (payload `PrimMethods`). Chưa làm: intern pointer/array/struct (không cần cho tính đúng hiện tại) |
| **P4** | Monomorphize thành worklist/instance graph thay vì clone AST rồi chèn đầu `declarations` | **một nửa** (`bddb880`): instance worklist + rewrite fixpoint, một danh sách `instances` duy nhất, thay `pending`/`work`/`inst_names`; test generic lồng nhau (`test_generics.kb`). Phần "instance graph thay clone" cần **IR có tham số hoá** (thay type arg ở codegen) — ngôn ngữ hiện chưa biểu diễn được, nên dừng ở đây |
| **P‑1** | Năng lực ngôn ngữ cần cho kiến trúc: payload enum (sum type), generic method/associated type, cho phép import vòng hoặc forward declaration | **xong**: forward declaration (`fe850db`, dùng cho `Type.methods` `4d28738`); **generic method** ở body pass (`121a335`); **payload enum** (`8801e08`); **payload enum cho `Type.data`** (`6de4052`) và **`AstNode.data`** (`fa01876`); **import vòng** (`00c1b8c`, test `test_loader_cycles.kb`, §3.5); **associated type** (`31bd32c`, test `test_associated_types.kb`, §6.6) |

**Vì sao P‑1 phải làm trước P2/P3.** Vì self-host, compiler hiện tại phải hiểu nguồn mới. Muốn viết
`Node`/`Type` bằng sum type an toàn hay chẻ module theo khái niệm (thay vì theo SCC vì cấm import vòng),
phải **thêm năng lực đó vào compiler hiện tại trước**, nếu không bản viết lại sẽ lặp lại đúng kiểu code cũ.

**Nợ kỹ thuật lộ ra khi P‑1 (generic method) — đã trả.** `find_method_template` từng chọn template theo
**tên**, không theo kiểu receiver, nên hai generic method cùng tên tranh nhau theo thứ tự nạp module
(gặp thật: `Type.to<T>` bị trỏ nhầm sang `AstNode.to<T>` khi `sema.types` import `ast.node`). Trước mắt
`Type` chuyển sang accessor `as_pointer/as_array/as_struct/as_struct_mut/as_fn/as_fn_mut/as_enum`
(`20a9341`) để `Symbol.ast_node`, `MethodInfo.ast_node`, `StructField.default_val` định kiểu được
`*AstNode`; gốc được sửa ở `121a335`: template ghi `self_type`, lookup khớp theo kiểu receiver, và
generic method được instantiate trong body pass rồi thay node qua `resolved_node`.

**Payload enum trong type model (đã làm, `6de4052`).** `Type.data` từ `*u8` thành `TypePayload`
(`NONE | Pointer | Array | Struct | Fn | Enum`), payload struct forward-declared trước enum; accessor là
pattern match, `is_enum` đọc variant `Enum`, `type_payload` thay generic `alloc_type<T>`. Cùng commit:
alias của arm `when` bind được trên **mọi biểu thức subject** (`when (h.shape) { is Shape.Circle as c -> ... }`),
không chỉ subject là identifier — checker và codegen cùng nới điều kiện, subject chỉ cần có tên khi
*rebind chính nó*.

**Payload enum trong AST (đã làm, `fa01876`).** `AstNode.data` từ `*u8` thành `NodeData`, một variant
cho mỗi hình dạng node (44 variant, payload là `*Shape`). Payload struct **khai báo trước** trong
`ast.node` và được định nghĩa ở module khác (forward declaration xuyên module), nên **không cần import
vòng**; mỗi variant có accessor `as_<shape>()`. `arena.make<T>` bị thay bằng
`arena.alloc_node(kind, NodeData.<Shape>(...), line, col)` vì factory generic không chọn được variant từ
`T`. Pattern `is LiteralExpr` trên `AstNode` bind `node.data.u.LiteralExpr.inner` và so tag qua C name
thật của hằng `TAG` (struct có thể được định nghĩa ở module khác nơi khai báo trước).

**P‑1 hoàn tất.** Cả bốn năng lực đã có: forward declaration (trong và xuyên module), generic method
phân giải ở body pass, payload enum (dùng cho `Type.data` và `AstNode.data`), import vòng và associated
type. Hệ quả kiến trúc: không còn lý do chẻ module theo SCC; `Type`/`AstNode` không còn payload `*u8` do
thiếu năng lực ngôn ngữ. **Không còn heuristic tên** ở backend: typedef do `emit_typedef` quyết định, tag
của pattern là field đầu tiên (`a1b5566`, §6.1).

**Definition of done của P1 (đã đạt):** `infer_type_from_expr`, `pointer_vars`, `str_vars`,
`lookup_fn_ret_type` = 0 call site; 16 snapshot chương trình không đổi byte nào khi thay đổi cơ chế.
Heuristic tên đã về 0 (`a1b5566`): nhánh `has_data` (payload `data: *u8` kiểu cũ) xoá ở `eef948f`, tag nay
là **field đầu tiên** của struct, và typedef do `StructDecl.emit_typedef` quyết định (thay cho tập tên
`is_struct_name`).

**Definition of done của P2 (phần I1, đã đạt):** 0 call site `replace_node`, 0 phép gán `kind`/`data`;
mọi hạ cấp đi qua `lower` (hoặc qua `resolved_node` mà check đã kiểm). Sema chỉ còn sửa *field*:
đổi tên identifier sang tên C, chèn `type_annotation`, bọc `(*p)` — ghi chú, không thay node.

**Suy luận kiểu trả về của arrow-fn.** `fn f() => expr` chưa có kiểu trả về cho tới khi thân được
kiểm. `infer_arrow_returns` chạy **trước** khi kiểm thân hàm: kiểm biểu thức thân trong scope riêng
(module + `Self` của khai báo), và nếu gặp lời gọi tới một arrow fn khác chưa có kiểu thì đánh dấu lần
thử là "chưa xong", **rollback chẩn đoán** rồi thử lại ở vòng sau — một fixpoint trên đồ thị gọi.
Hàm suy luận xong được đánh dấu đã kiểm, nên pass chính bỏ qua; chu trình bị báo lỗi rõ ràng
("annotate it explicitly") thay vì đệ quy vô hạn. Nhờ vậy không còn việc kiểm thân callee từ trong lúc
kiểm lời gọi (re-entrancy cũ).

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
- **AST Extension Methods & Node Downcast**: Toàn bộ các factory method tạo AST node (`arena.literal(...)`, `arena.binary(...)`, `arena.fn_decl(...)`...) được định nghĩa trực tiếp dưới dạng extension method trên `impl Arena` (trong `compiler.ast.builder`), loại bỏ struct `AstBuilder` bao bọc bên ngoài. Hỗ trợ hàm generic downcast `node.to<T>()` và hàm generic factory `make<T>(arena, ...)` / `arena.make<T>(kind, data, line, col)` (desugar tự động sang monomorphized generic call), giúp thu gọn toàn bộ các hàm ép kiểu và cấp phát riêng lẻ (`node.to<LiteralExpr>()`, `arena.make<BinaryExpr>(...)`...). Toàn bộ 50 hàm cấp phát tự do cũ `alloc_*(&arena, ...)` và 74 hàm/method downcast cũ (`as_*` / `node.as_*`) cùng các hàm alloc rời rạc trong sema types đã được xóa sổ hoàn toàn khỏi codebase; toàn bộ các method tạo node được rút gọn thành 1-liner expression body ủy quyền qua `make<T>(*self, ...)`.
- **`yield` trong biểu thức `when`**: Hỗ trợ từ khóa `yield <expr>;` bên trong các nhánh block `{ ... }` của biểu thức `when` (pattern matching). Cho phép một nhánh thực hiện tính toán nhiều bước với biến cục bộ trước khi sinh ra giá trị trả về cho biểu thức (`val x = when (...) { ... yield res; }` hoặc `return when (...) { ... yield res; }` / `fn f() => when (...) { ... yield res; }`). Codegen tự động hạ cấp cấu trúc `when` phức tạp thành chuỗi `if-else` C99 tối ưu và chuẩn xác.
- **Toán tử gán kết hợp (`+=`, `-=`, `*=`, `/=`, `%=`)**: Hỗ trợ đầy đủ cho các kiểu số học và nối chuỗi (`str += str` được sema tự động hạ thành `kobel_concat`). Backend C99 phát sinh trực tiếp các toán tử gán kết hợp tương ứng; toán tử được lưu trữ trong `AssignExpr.op` (`TokenType`). Đã được xác nhận đạt chuẩn self-host fixpoint 100%.
- **Toán tử tăng giảm (`++`, `--`)**: Hỗ trợ dạng câu lệnh độc lập (`x++;`, `x--;`, `self.field++;`, `list[i]--;` theo phong cách Go). Áp dụng cho mọi kiểu số (`i8..i64`, `u8..u64`, `isz`, `usz`, `f32`, `f64`). Với biểu thức chỉ mục trên struct (`list[i]++`), sema tự động desugar sang phương thức `set` (`list.set(i, list.get(i) + 1)`). Đã đạt chuẩn bootstrap test suite 100%.
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
