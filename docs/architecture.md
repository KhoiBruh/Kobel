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
| `collections/list.kb` | `List<T>` (generic) + `new_list<T>` |
| `collections/hash_map.kb` | `HashMap<V>` |
| `collections/string_builder.kb` | `StringBuilder` |
| `mem/alloc.kb` | **facade cấp phát** (chưa kiểm soát): byte `raw_alloc/raw_resize/raw_release`; typed `alloc<T>/alloc_array<T>/resize<T>/release<T>` |
| `mem/arena.kb` | `Arena` (vùng, giải phóng một lần) + `arena_alloc<T>(&Arena): *T`; dùng `raw_*` của facade |

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
| `obj.method(args)` | `Struct_method(&obj, args)` (thêm `&` nếu `obj` là value) |
| `a + b` với `a: str` | `kobel_concat(a, b)` |
| `a == b` / `a != b` với `str` | `kobel_streq(a, b)` / `!kobel_streq(a, b)` |
| `s.len` / `s.size` (`s: str`) | `kobel_slen(s)` |
| `s.slice(a, b)` | `kobel_slice(s, a, b)` (`b` mặc định `kobel_slen(s)`) |
| `T.size()` trong code generic | literal theo kích thước C |
| `arr.len` / `arr.size` (`arr: [T; N]`) | literal `usz` (kích thước biết lúc biên dịch) |
| `s.data` / `arr.data` (`str`, `[T; N]`) | chính biểu thức đó (con trỏ tới phần tử đầu) |
| `for (x in seq)` | `val __for_n = seq.len; var __for_i = 0; while (__for_i < __for_n) { val x = seq.data[__for_i]; …; __for_i += 1 }` |
| `for (i in a..b)` / `a>..<b` | `while (__for_go) { … }` có cờ kết thúc; chiều tăng/giảm quyết định **lúc chạy** (`__for_up`) |

`STMT_FOR` **không** đi tới codegen: `body_pass` hạ nó thành block + `while` ngay khi kiểm tra, nên
backend C không cần biết gì về `for`. Bốn ghi chú ngữ nghĩa của pha 1:

- **Chỉ nhận lvalue** làm đối tượng duyệt (`x in self.items` được, `x in f()` không): thân vòng
  dùng lại biểu thức đó mỗi vòng, nên biểu thức tạm sẽ treo còn lời gọi hàm sẽ chạy lặp.
- **Độ dài chốt lúc vào vòng** (`__for_n`): vòng không giãn ra nếu thân vòng thêm phần tử; với
  `str` việc này còn bỏ được một lời gọi `strlen` mỗi vòng.
- **`continue` vẫn bước tiếp biến đếm**: `for_inject_step` chèn bước nhảy vào trước mọi `continue`
  thuộc vòng này (không đụng `continue` của vòng lồng bên trong).
- **Đếm an toàn tràn số**: bước nhảy chỉ chạy khi cờ "còn phần tử" còn đúng, nên biến đếm unsigned
  không bao giờ giảm xuống dưới biên.

`for` hiện chưa có dạng nửa mở (`a..<b`) — xem §10.

### 6.3 Runtime helper trong C sinh ra

Prelude của file C chèn sẵn: `kobel_slice`, `kobel_concat`, `kobel_streq`, `kobel_slen`
(hiện nhúng dưới dạng chuỗi trong `c_codegen.kb`; dự kiến tách ra `codegen/runtime.c.inc`).

### 6.4 Hạ tầng C

| Khái niệm Kotel | C sinh ra |
|---|---|
| `struct S` | `typedef struct S S;` + `struct S { … };` |
| `enum E` | `typedef int32_t E;` (thành viên → hằng số) |
| `const X = …` cấp module | `#define X …` |
| mọi thứ khác (fn, method) | tên mangle `<module__>Name`, method `<StructC>_<method>` |
| `*T` (read-only) | `T*` — **trừ** `*char`/`*str` giữ `const` (nhận string literal / `.c_str()`) |
| `str` | `const char*` |
| `T.size()` | `sizeof(<tên C>)` |

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
- **Monomorphization** (generic):
  - Pass 0 đăng ký *template* (`struct`/`fn`/`impl` có `<T>`), kèm `module`.
  - `List<X>` ở vị trí kiểu → instantiate; `f<X>(...)` / `Struct<X>(...)` → instantiate ở pass quét AST.
  - Instance sinh bằng **clone AST + thay type param**, tên C = `<mangle(template)>_<token(type arg)>`,
    trong đó token lấy từ **tên C đã mangle** của type arg (tránh trùng tên giữa các module).
  - Instance đăng ký vào `inst_syms` (tra cứu toàn cục) và được **chèn lên đầu** `program.declarations`
    để struct instance đứng trước struct dùng nó.
  - Chỉ hỗ trợ **type argument tường minh**; không suy luận type arg.

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
| Test v1 | `scripts/test_all_bootstrap.bat` (8 test) |
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

- **Generics**: chỉ type arg tường minh; generic impl phải cùng tên struct template; chưa hỗ trợ
  trait/bounds; `T.size()` hạ thành literal theo layout của v1 (khớp thực tế cho các kiểu đang dùng).
- **`for`**: chỉ duyệt lvalue; chưa hỗ trợ `Map`/`HashMap` (kho lưu thưa, cần cursor) và
  `for ((k, v) in map)` (cần destructuring). **Chưa có dạng nửa mở `a..<b`** — đây là lý do chính
  khiến phần lớn vòng index kiểu `while (i < n)` trong nguồn compiler chưa hạ sang `for` được:
  dạng đóng tương đương sẽ là `0..n-1`, và `n - 1` tràn khi `n` là unsigned bằng 0.
- **Generic method** (`impl S { fn f<T>() }`) **không** được hỗ trợ: tham số `T` rò nguyên vào C
  (`error C2065: 'T' undeclared`). Vì vậy cấp phát có kiểu phải là **free generic function**
  (`alloc<T>(&arena)`), không thể là method `arena.alloc<T>()`.
- **`none`**: đã thay `void` ở cả v0/v1; `void` giờ là lỗi biên dịch.
- **Kiểu hàm bậc nhất** (`fn` type) chưa dùng trong `ast_type_from_type` (trả `null` → codegen tự suy).
- Cảnh báo C khi build ở `/Wall`: còn `C5045` (Spectre note), `C4820` (padding) — mang tính thông tin.
- Hai bản cài đặt sema (v0 và v1) song song ⇒ nguy cơ lệch hành vi; giảm dần bằng cách đóng băng seed.
