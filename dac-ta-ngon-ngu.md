# Đặc tả ngôn ngữ lập trình (chưa đặt tên)

> Bản cập nhật — tổng hợp toàn bộ quyết định thiết kế tính tới thời điểm hiện tại. Đủ để bắt đầu viết compiler v0 (bằng C++ + LLVM). Các mảng còn treo được liệt kê ở chương cuối.

## 1. Tổng quan

- **Loại ngôn ngữ:** đa năng, thủ tục kết hợp hướng đối tượng — không có `class`, chỉ có `struct` (thuần túy chứa dữ liệu) và `impl` (chứa các hàm triển khai), cùng `trait` (trừu tượng, không virtual/vtable — mọi dispatch giải quyết tĩnh lúc biên dịch qua overload resolution).
- **Biên dịch:** xuống LLVM IR → mã máy native. Compiler v0 viết bằng C++ (dùng LLVM C++ API); mục tiêu dài hạn là bootstrap — viết lại compiler bằng chính ngôn ngữ (gọi LLVM qua C API ổn định `llvm-c` qua `extern`).
- **Đối tượng hướng tới:** cả người mới lẫn có kinh nghiệm — ưu tiên dễ hiểu, thân thiện, tránh phình cú pháp kiểu Java (không cho tự định nghĩa attribute mới, không cho enum mang dữ liệu vì đã có union...).
- **Quản lý bộ nhớ:** không GC — mỗi giá trị có đúng một chủ sở hữu, tự động giải phóng khi hết scope. Compiler chỉ kiểm tra move (Phương án A — không có borrow checker đầy đủ kiểu Rust).
- **Nền tảng mục tiêu:** Windows; Linux (x86-64, ARM64, RISC-V64); ESP32 dòng RISC-V (không hỗ trợ ESP8266/ESP32-Xtensa vì LLVM upstream không hỗ trợ Xtensa).
- **Runtime:** giai đoạn đầu phụ thuộc libc (qua `extern "libc"`). Chế độ "đứng độc lập" (tự gọi syscall trực tiếp, tự viết `_start`, không cần libc) đã bàn về cơ chế nhưng hoãn lại — quá phức tạp cho giai đoạn đầu, để dành làm tính năng tùy chọn sau này.

---

## 2. Cú pháp cơ bản

- Ngoặc nhọn `{}` định phạm vi khối lệnh.
- Comment: `// một dòng`, `/* nhiều dòng */`.
- Từ khóa chính: `fn`, `struct`, `trait`, `fault`, `enum`, `alias`, `const`, `val`, `var`, `mod`, `use`, `pub`, `extern`, `defer`, `as`, `as?`, `in`.
- Attribute (không gọi là "annotation"): `@operator(...)`, `@infix`. **Không cho phép người dùng tự định nghĩa attribute mới** — chỉ có tập cố định do ngôn ngữ cung cấp, tránh rối loạn/giảm hiệu năng kiểu hệ sinh thái annotation Java.

### Thân hàm dạng biểu thức đơn

```
fn add(a: i32, b: i32): i32 => a + b;
```

`=>` áp dụng cho mọi hàm, không đụng độ với arm của `when` vì chỉ xuất hiện đúng một vị trí ngữ pháp (ngay sau phần tham số của `fn`).

---

## 3. Kiểu dữ liệu nguyên thủy

| Nhóm | Kiểu |
|---|---|
| Số nguyên có dấu | `i8, i16, i32, i64, isz` |
| Số nguyên không dấu | `u8, u16, u32, u64, usz` |
| Số thực | `f16, f32, f64` |
| Luận lý | `bool` |
| Ký tự | `char` (1 byte ASCII) |
| Chuỗi | `str` (heap-allocated, sở hữu dữ liệu, mượn qua cơ chế `self/val self/var self` — không tách owned/borrowed như Rust) |
| Đơn vị | không có từ khóa riêng — hàm không khai kiểu trả về thì ngầm hiểu unit |

`isz`/`usz` = độ rộng theo kiến trúc máy (như `isize`/`usize` của Rust).

### Nullable

```
val a: i32? = null;
```

### Con trỏ thô

Dùng được ở mọi nơi, không giới hạn trong khối `unsafe`:
- `*T` — con trỏ chỉ đọc
- `&T` — con trỏ đọc/ghi

### Literal số

- Hỗ trợ hex (`0xFF`), binary (`0b1010`), dấu gạch dưới phân tách (`1_000_000`).
- Hậu tố kiểu (viết **IN HOA**, mặc định `i32`/`f64` nếu không ghi):

| Hậu tố | Kiểu |
|---|---|
| *(không, không `.`)* | `i32` (hoặc suy luận theo ngữ cảnh, xem bên dưới) |
| `S` | `i16` |
| `L` | `i64` |
| `B` | `i8` |
| `Z` | `isz` |
| `U` | `u32` |
| `US` | `u16` |
| `UL` | `u64` |
| `UB` | `u8` |
| `UZ` | `usz` |
| *(không, có `.`)* hoặc `D` | `f64` |
| `F` | `f32` |

**Suy luận kiểu theo ngữ cảnh:** literal số nguyên **không có hậu tố** sẽ lấy kiểu của ngữ cảnh xung quanh thay vì luôn mặc định `i32`. Nhờ đó không cần viết `UZ`/`L` khi kiểu đã rõ ràng:

```
val a = i32.size() * 2;      // 2 suy luận là usz, a: usz
val b: usz = 2 * 3;          // cả cụm literal suy luận là usz
var c: usz = 0;
c = c + 1;                   // 1 suy luận là usz
val d = takes_usz(2 * 3);    // tham số suy luận là usz
```

Quy tắc:

- Một biểu thức chỉ gồm literal không hậu tố (kể cả ngoặc và `+ - * / %` giữa chúng) được coi là "literal thuần" và nhận kiểu của ngữ cảnh.
- Ngữ cảnh gồm: toán hạng còn lại của phép toán 2 ngôi, kiểu khai báo `val`/`var`/`const`, kiểu trả về, kiểu tham số hàm/struct, `return`, gán, nhánh `if`/`when`.
- **Hậu tố tường minh luôn thắng**: `x + 1UZ` (với `x: usz`) vẫn hợp lệ, còn `x + 1UZ` (với `x: i64`) vẫn báo lỗi.
- Hai toán hạng đều là biến/giá trị có kiểu khác nhau vẫn **không** tự động chuyển kiểu — phải dùng `as`.
- Literal âm (ví dụ `-1`) chỉ suy luận được cho kiểu **có dấu**; `val x: usz = -1;` vẫn là lỗi.

### Ép kiểu

```
val b = a as u32;    // panic nếu vượt phạm vi / không khớp (vd: số -> enum)
val c = a as? u32;    // trả về nullable thay vì panic
```

Ép kiểu mở rộng (widening) luôn thành công. Không có toán tử ép kiểu "thô" bỏ qua kiểm tra (đã cân nhắc rồi bỏ).

---

## 4. Kiểu dữ liệu phức hợp

| Kiểu | Ý nghĩa | Vùng nhớ |
|---|---|---|
| `Array<T>` | Cố định, biết size lúc biên dịch | Stack-like |
| `Slice<T>` | Cố định, biết size lúc chạy | Heap, cấp 1 lần |
| `List<T>` | Động, tự resize | Heap |
| `Map<K, V>` | Không ràng buộc `K`, tra cứu kiểu association-list (dùng `==`), O(n) | Heap |
| `HashMap<K, V>` | `K: Hashable`, bảng băm thật, tra cứu nhanh | Heap |

```
val a: Array<i32> = [0, 1, 2, 3];             // suy luận size = 4
val b: Array<i32>(4) = [0, 1, 2, 3];           // size tường minh, phải khớp chính xác
val m: Map<str, i32> = [ "a" to 1, "b" to 2 ]; // "to" là infix function built-in (cần use)
```

Size khai báo tường minh không khớp số phần tử literal → lỗi biên dịch (không auto-fill giá trị mặc định, vì không phải kiểu nào cũng có default hợp lý).

### `Array`/`str` nằm trong prelude (không cần import); `Slice`/`List`/`Map`/`HashMap` cần `use`.

### Struct tự tham chiếu (linked list, cây...)

```
struct Node {
    value: i32,
    next: *Node?
}
```

Dùng con trỏ thô nullable, cấp phát/giải phóng thủ công qua `extern "libc"` + `defer`. Chưa có smart pointer sở hữu tự động (kiểu `Box<T>` của Rust) — để dành làm sau.

### Iterator

Trait riêng, bắt buộc với `Array`, `Slice`, `List`, `Map`, `HashMap`, range (`0..10`...) để dùng được trong `for (... in ...)`.

---

## 5. Struct, Trait, Fault, Enum

### Struct — cú pháp Rust-like và `impl`

```
struct A {
    pub a: i32,
    pub b: str
}

impl A {
    fn new(c: i32 = 1, d: str = "hello"): Self {
        return Self(c, d);
    }
    // hoặc gọn:
    fn new(c: i32 = 1, d: str = "hello") => Self(c, d);
}
```

- Struct thuần túy chứa khai báo các trường dữ liệu (fields), hỗ trợ từ khóa `pub` cho từng field.
- Toàn bộ hàm thành viên (methods) được định nghĩa tách rời trong khối `impl StructName { ... }`.
- Cú pháp cũ kiểu `struct Point(...) : Trait { ... }` đã được loại bỏ hoàn toàn để mã nguồn mạch lạc và giống Rust hơn.
- **Không có `new(...)`:** khởi tạo truyền đủ mọi field theo đúng thứ tự khai báo: `A(1, "hello")`.
- **Có `new(...)`:** khởi tạo theo đúng chữ ký `new`. `Self(...)` bên trong luôn trỏ tới chính struct đang định nghĩa.
- Layout bộ nhớ: **luôn theo chuẩn C** (thứ tự field, alignment/padding tự nhiên) — áp dụng mặc định cho mọi struct, không cần đánh dấu riêng. Mục đích: dễ FFI/bind sang C, Java, Python...

### Trait

```
trait B {
    fn greet(val self): str => "hello";  // có triển khai mặc định
}

struct A {
    pub a: i32,
    pub b: str
}

impl B for A {
    fn greet(val self): str => "hi, " + self.b;
}
```

- Có thể có triển khai mặc định.
- Một struct implement được nhiều trait cùng lúc.
- Trait có thể kế thừa trait khác.

### Fault (kiểu lỗi)

```
fault A;                     // không mang dữ liệu
fault A(message: str);       // mang dữ liệu, giống struct
fault B : A;                 // "mở rộng" — chỉ mang tính tổ chức/phân loại
```

- **Không phải kiểu thật** — không dùng trait được cho fault.
- Quan hệ `:` **không phải** is-a subtyping: throw/catch luôn theo đúng kiểu cụ thể. Ném `B` thì chỉ bắt được bằng nhánh `B`, không tự động khớp `A`. Không tự động gộp vào union return type của kiểu cha.

### Enum

```
enum Status : u16 {   // ": u16" có thể bỏ trống để compiler tự chọn kiểu tối ưu
    A,        // 0
    B = 504,
    C,        // 505 (tự tăng)
    D,        // 506
}

Status.A.value      // lấy số nguyên
Status.of(505)       // trả về Status? (nullable, không phải fault — chỉ 1 lý do thất bại)
```

- **Không mang dữ liệu thêm** (khác Java/Kotlin) — vai trò đó do union struct đảm nhiệm (`Circle | Square`), tránh 2 cách biểu diễn trùng khái niệm sum type.

### `alias` — đa năng

```
alias Number = Comparable + Cloneable;   // gộp nhiều trait bound, dùng trong <T: Number>
alias Meters = f64;                       // type alias/typedef đơn giản
alias Shape = Circle | Square | Triangle; // đặt tên cho union type
```

---

## 6. Ownership & Quản lý bộ nhớ

### Ba chế độ tham số

| Khai báo | Ý nghĩa | Nhận sở hữu? |
|---|---|---|
| `self: T` | Move | Có — hết hàm tự giải phóng (nếu có hàm dọn) hoặc cần `defer` |
| `val self: T` | Mượn đọc | Không |
| `var self: T` | Mượn đọc/ghi | Không |

Compiler chỉ kiểm tra **move** (Phương án A) — không có borrow checker/lifetime kiểu Rust. Lập trình viên tự chịu trách nhiệm về mượn chồng chéo (không gây lỗi memory-unsafe, chỉ có thể gây logic bug).

### `val`/`var` biến cục bộ — độc lập với ownership

- Chỉ kiểm soát việc **gán lại binding**, không liên quan tới move/mutable của dữ liệu.
- `val a` bị move → chết vĩnh viễn (không gán lại được để hồi sinh).
- `var b` bị move → gán giá trị mới, dùng lại bình thường.
- `val a = f(...)` (a không gán lại được) vẫn truyền được vào tham số `var self`/`var param`/tham số sở hữu — vì việc đó không "gán lại a".

### `const` (khác `val`)

Chỉ khai báo ở top-level (không cho biến toàn cục nào khác ngoài `fn main()`). Giá trị phải tính được lúc biên dịch, hoặc là struct literal không cần giải phóng. **Không được gọi hàm** để khởi tạo.

### `defer`

Struct không có hàm dọn riêng → phải `defer <hàm dọn>(...)` thủ công. Có hàm dọn → tự động gọi khi hết scope.

---

## 7. Generic

```
fn max<T: Comparable>(val a: T, val b: T): T { ... }
struct Box<T: Comparable> { value: T }
```

Không có `where` clause. Cho phép nhiều trait bound (`T: A + B`, hoặc qua `alias` gộp sẵn). Áp dụng cho cả hàm và struct.

---

## 8. Kiểm soát luồng

```
val a = if (x > 0) 1 else -1;   // if/else là biểu thức

for (i in 0..10) { ... }        // đóng-đóng
for (i in 0>..<10) { ... }      // mở-mở
for (i in 10..0) { ... }        // ngược, đóng-đóng
for (i in 10>..<0) { ... }      // ngược, mở-mở
for (i in iterator) { ... }
for ((k, v) in map) { ... }     // destructuring

while (cond) { ... }
loop { ... break; }             // không có do-while

val result = when (x) {          // when là biểu thức, dùng độc lập được
    1 -> "one";
    2, 3 -> "two or three";
    else -> "other";
};
```

`break`/`continue` hỗ trợ label để thoát vòng lặp lồng nhau (cú pháp label cụ thể chưa chốt).

### Destructuring

```
val (a, b) = getPair();
```

---

## 9. Toán tử & Overloading

```
struct A {
    pub a: i32,
    pub c: i32
}

impl A {
    @operator(+)
    fn plus(val self, other: A): A => A(self.a + other.a, self.c + other.c);
}
```

- Không cần trait riêng — `@operator(...)` quyết định dispatch, tên hàm tùy ý.
- Hỗ trợ toán tử số học và so sánh. Sai kiểu/thiếu impl → lỗi biên dịch.

### Infix function tùy chỉnh

```
@infix
fn to(val self, other: B): Pair<A, B> => Pair(self, other);

val p = a to b;   // desugar thành a.to(b)
```

`to` là built-in nhưng vẫn cần `use` để dùng. `in` là cú pháp cứng của ngôn ngữ, không phải infix function.

---

## 10. Xử lý lỗi & Null Safety

```
fn parse(s: str): i32 | IllegalArguments {
    if (invalid) { return IllegalArguments(...); }
    return 5;
}

val a = parse(s) !: 0;                            // fallback giá trị
val b = parse(s) !: return IllegalArguments(...);  // return sớm
val c = parse(s) !: when {
    IllegalArguments -> ...;
};                                                  // phân biệt theo loại lỗi

val d = maybeNull() ?: 0;   // Elvis cho null

val e = parse(s);   // không xử lý -> panic, in toàn bộ thông tin lỗi rồi sập
```

Không dùng exception (`try/catch`) — lỗi là một phần kiểu trả về (error-as-value, union type với `fault`).

---

## 11. Module System

```
mod a.b.c;   // c = tên file, a.b = đường dẫn thư mục

use a.b.c.A;
use b.c.d.A as B;
use b.c.d.*;     // chỉ kéo vào những gì thực sự dùng

use a.b.B;
use a.c.B;        // trùng tên -> dùng qua tiền tố module cuối: b.B và c.B
```

- Nếu tiền tố module cuối cũng trùng nhau → bắt buộc `use ... as <alias>`. **Không cho phép** viết đường dẫn đầy đủ để giải quyết trùng tên (làm code xấu).
- Visibility: chỉ có `pub` (không có `protected`/`internal`).

---

## 12. FFI

```
extern "libc" {
    fn printf(fmt: *char, ...): i32;
    fn malloc(size: usz): *u8;
}

pub extern fn compute(x: i32): i32 {
    return x * 2;
}
```

- Hỗ trợ tham số biến đổi (`...`) khi **gọi vào** hàm C — ngôn ngữ này không tự định nghĩa hàm variadic riêng.
- `str` khác `*char` của C (có độ dài kèm theo, không null-terminated) — cần hàm chuyển đổi trong thư viện chuẩn.
- Hàm export (`pub extern fn`) bắt buộc có thân hàm, **không được overload** (C ABI cần tên symbol duy nhất).
- Layout struct đã theo chuẩn C mặc định (chương 5) nên tương thích FFI ngay không cần đánh dấu thêm.

---

## 13. `fn main()`

Hai biến thể: không nhận tham số dòng lệnh, hoặc có nhận tham số dòng lệnh (chi tiết chữ ký cụ thể — kiểu dữ liệu tham số — sẽ chốt khi viết ngữ pháp chi tiết).

---

## 14. Lộ trình bootstrap compiler

1. **v0** — viết bằng C++, dùng LLVM C++ API. Chỉ cần hỗ trợ tập con tối thiểu: struct, trait, generic cơ bản, control flow, error/null handling, module, FFI, `Map`/`HashMap`.
2. **v1** — viết lại mã nguồn compiler bằng chính ngôn ngữ mới (dùng LLVM C API qua `extern` thay vì C++ API), biên dịch bằng v0.
3. **v2** — dùng v1 tự biên dịch lại chính mã nguồn của nó, xác nhận kết quả nhất quán → self-hosting thành công, không cần v0/C++ nữa.
4. Từ đó, thêm tính năng mới cho ngôn ngữ theo chu trình 2 nhịp: (a) viết code xử lý tính năng X vào compiler bằng những gì đã có (chưa dùng X), dùng compiler hiện tại biên dịch ra bản mới hiểu X; (b) từ bản mới trở đi, mã nguồn compiler được phép tự dùng X.

---

## 15. Những phần còn để ngỏ

- **Lambda/closure** — dự định cú pháp kiểu Kotlin (`list.filter { it.is_blank() }`), cơ chế capture biến ngoài (theo giá trị hay tham chiếu, có cần khai báo tường minh) **chưa quyết định** — tạm gác, xem là tính năng cao cấp làm sau.
- **Chế độ freestanding/no-libc** — đã bàn cơ chế (`syscall(...)` intrinsic, tự viết `_start`) nhưng hoãn, không nằm trong scope ban đầu.
- **Cú pháp label cụ thể** cho `break`/`continue`.
- **Chữ ký chi tiết của `main(args: ...)`** — kiểu dữ liệu tham số dòng lệnh.
- **Smart pointer sở hữu** (kiểu `Box<T>`) cho struct tự tham chiếu — hiện dùng con trỏ thô thủ công.
- **Concurrency/threading** — mô hình chưa bàn (thread thô qua libc, hay cú pháp riêng như `async`?).
- **Field-level visibility** — `pub` áp được cho từng field riêng trong struct không, hay chỉ cho cả struct/hàm.
- **Doc comment** sinh tài liệu tự động (`///`).
- **Tên ngôn ngữ, đuôi file mã nguồn** — chưa quyết định.
- **Package manager/build system** cho dự án nhiều file.
