# Kobel Language Specification

> The reference implementation is the self-hosted compiler in `src/`. Where this specification and the compiler disagree, this document is updated to match compiler reality or an issue is opened.

---

## 1. Overview

- **Paradigm:** Procedural with object-oriented and functional features. No `class` keyword: `struct` holds data, `impl` holds behavior, and `trait` provides static abstraction.
- **Dispatch:** 100% static compile-time dispatch. No virtual method tables (vtables), no runtime type overhead.
- **Compilation model:** Kobel source (`.kb`) → C99 source → native binary via any C compiler (`cl` by default on Windows, `gcc`/`clang` on POSIX).
- **Memory model:** Explicit ownership without garbage collection. Values have single ownership and are freed at scope end. Raw pointers (`*T` read-only, `&T` mutable) are available for low-level systems programming.
- **Error model:** Return values and null-safety operators (`?.` and `?:`). Rich fault/error union types are planned for a future release (see §13).

---

## 2. Lexical Structure

- **Blocks:** Delimited by curly braces `{ ... }`.
- **Comments:** Line comments `// ...` and block comments `/* ... */`.
- **Keywords:**
  `as`, `break`, `const`, `continue`, `defer`, `else`, `enum`, `extern`, `false`, `fn`, `for`, `if`, `impl`, `in`, `init`, `is`, `mod`, `null`, `op`, `pub`, `return`, `self`, `Self`, `struct`, `trait`, `true`, `use`, `val`, `var`, `when`, `while`, `yield`.
- **Identifiers:** `[A-Za-z_][A-Za-z0-9_]*`. Module segments must be valid identifiers (keywords cannot be module path segments).

### Literals

- **Integers:** Decimal (`42`), hex (`0xFF`), binary (`0b1010`), and digit separators (`1_000_000`).
- **Suffixes (uppercase):**
  - Signed: `B` (`i8`), `S` (`i16`), `L` (`i64`), `Z` (`isz`).
  - Unsigned: `UB` (`u8`), `US` (`u16`), `U` (`u32`), `UL` (`u64`), `UZ` (`usz`).
  - Default: `i32` when unsuffixed.
- **Contextual numeric inference:** An unsuffixed numeric literal adapts to the expected type from its surrounding context (e.g. variable annotation, function argument, binary operation):
  ```kobel
  val a: usz = 0;   // 0 infers to usz
  val b = a + 1;    // 1 infers to usz
  ```
- **Floats:** `f64` by default (`3.14`, `1.0D`), or `F` suffix for `f32` (`3.14F`).
- **Booleans:** `true`, `false`.
- **Characters:** ASCII character literal `'a'`. Escape sequences: `\n`, `\r`, `\t`, `\\`, `\'`, `\"`, `\0`.
- **Strings:** Double-quoted `"hello"`. Strings are owned and heap-allocated.

---

## 3. Primitive Types

| Group | Types | Notes |
|---|---|---|
| Signed integers | `i8`, `i16`, `i32`, `i64`, `isz` | `isz` matches pointer width |
| Unsigned integers | `u8`, `u16`, `u32`, `u64`, `usz` | `usz` matches pointer width |
| Floating point | `f32`, `f64` | IEEE-754 |
| Boolean | `bool` | `true` or `false` |
| Character | `char` | 1-byte ASCII character |
| String | `str` | Length-tagged string |
| None | `none` | Unit / no return value |

### String Features

- **String interpolation:** Inside `"..."`, `${expression}` interpolates any value whose type implements the `Stringer` trait (primitives, numbers, and custom types):
  ```kobel
  val name = "Kobel";
  val count = 42;
  println("Hello ${name}, count = ${count}");
  ```
  Use `\$` for a literal dollar sign.
- **Adjacent string concatenation:** String literals separated only by whitespace or newlines merge at compile time:
  ```kobel
  val query = "SELECT id, name "
              "FROM users "
              "WHERE active = 1;";
  ```

### Nullable Types and Pointers

- **Raw pointers:**
  - `*T`: Non-null read-only pointer (dereferenced via `*p` or `p.field`).
  - `&T`: Non-null mutable pointer.
- **Nullable pointers:**
  - `?*T`: Nullable read-only pointer (e.g., `?*Node`, `?*?*Node`, `*?*Node`).
  - `?&T`: Nullable mutable pointer (e.g., `?&Node`, `&?*Node`).
  - Only pointer types may be nullable; non-pointer value types cannot hold `null`.
- **Safe navigation (`?.`):** Accesses a field or calls a method only if the pointer is not null; evaluates to `null` or a default if null.
- **Elvis operator (`?:`):** Provides a fallback value or early return on null:
  ```kobel
  val name = node?.name ?: "anonymous";
  val next = node?.next ?: return false;
  ```
- **Casts:**
  - `x as T`: Casts `x` to type `T`. Type checking and downcasting are performed via pattern matching and smart casting with `is` / `!is` (`if (x is T)`, `if (x is [A, B])`, `if (x !is T) ... else ...`, or `when (x) { is T -> ...; is [A, B] -> ...; !is U -> ... }`).

---

## 4. Structs, Methods, and Traits

### Structs and Implementation

```kobel
pub struct Point {
    pub x: i32,
    pub y: i32 = 0    // default field value
}

impl Point {
    // Constructor (implicitly returns Self; return type cannot be declared)
    pub init(x: i32, y: i32 = 0) => Self(x, y);

    // Static helper
    pub fn origin(): Self => Self(0, 0);

    // Instance method (read-only pointer receiver)
    pub fn length_squared(*self): i32 => self.x * self.x + self.y * self.y;

    // Mutating instance method (mutable pointer receiver)
    pub fn translate(&self, dx: i32, dy: i32) {
        self.x += dx;
        self.y += dy;
    }
}
```

- **Fields:** Declared inside `struct`. Fields support `pub` visibility and default initializers.
- **Constructors:** Declared with `init(...) [=> expr; | { ... }]`. Constructors never declare a return type (specifying a return type is a syntax error); they implicitly return an instance of `Self`. Instances are created via `TypeName(...)`.
- **Methods:** Defined in `impl StructName` blocks.
  - Instance methods require an explicit receiver:
    - `*self`: Read-only pointer receiver (`const T*`).
    - `&self`: Mutable pointer receiver (`T*`).
    - bare `self`: Consuming/move receiver (moves/consumes the caller variable).
  - Static methods omit `self` and are called via `TypeName.method(...)` or `Self.method(...)` within `impl`.
- **Construction:**
  - Unified constructor syntax: `Point(1, 2)` calls `Point.new(1, 2)` if defined, otherwise falls back to positional field construction.
  - Explicit static call: `Point.new(1, 2)`.
  - Raw struct construction: `Self(1, 2)` inside `impl` constructs fields directly without calling `new`.

### Forward Declarations

Mutually referential structs can be forward-declared:
```kobel
struct A;

struct B {
    pub a: *A
}

struct A {
    pub b: ?*B = null
}
```

### Traits

Traits define shared interfaces with static dispatch:

```kobel
trait Greeter {
    fn name(*self): str;
    // Default implementation:
    fn greet(*self): str => "Hello, " + self.name();
}

impl Greeter for Point {
    fn name(*self): str => "Point(${self.x}, ${self.y})";
}
```

- Traits support inheritance: `trait B : A { ... }`.
- Traits can provide default method implementations that implementors may override.
- Trait bounds on generics: `fn show<T: Greeter>(val item: T) { ... }`.

### Associated Types

Traits can declare associated types, resolved per implementation:

```kobel
trait Container {
    type Item;
    fn first(*self): Self.Item;
}

impl Container for IntBox {
    type Item = i32;
    fn first(*self): Self.Item => self.value;
}
```

---

## 5. Enums

### Plain Enums

```kobel
enum Status {
    OK,
    NOT_FOUND = 404,
    ERROR
}

val s = Status.OK;
val code = s.value;    // underlying integer value
```

### Payload Enums (Tagged Unions)

```kobel
enum Shape {
    Circle(radius: f64),
    Rect(width: f64, height: f64),
    Empty
}

val c = Shape.Circle(2.5);

val area = when (c) {
    is Shape.Circle as circle -> 3.14159 * circle.radius * circle.radius;
    is Shape.Rect as r -> r.width * r.height;
    is Shape.Empty -> 0.0;
    else -> 0.0;
};
```

- Variants can store named payload fields.
- `is Enum.Variant as alias` binds a pointer to the variant payload.
- `!is Enum.Variant` tests the negation of a type or variant pattern (desugared to `!(x is Variant)`); `else` branches of `if (x !is Variant)` smart-cast `x`, and `as` alias binding is rejected with `!is`.
- `is [A, B, ...]` and `!is [A, B, ...]` test whether a subject matches (or does not match) any type or variant in the list (with optional `.Variant` shorthand for payload enums and single-element smart casting); `as` alias binding and empty `[]` lists are rejected.

---

## 6. Generics

Kobel supports generic structs, functions, and methods monomorphized at compile time:

```kobel
struct Box<T> {
    pub value: T
}

impl Box<T> {
    pub fn get(*self): T => self.value;
}

fn identity<T>(val x: T): T => x;

// Generic method on struct:
impl Arena {
    pub fn alloc<T>(&self): *T { ... }
}
```

- Type arguments can be explicitly passed (`box.get<i32>()`) or inferred from context (`arena.alloc_val(42)`).

---

## 7. Operators and Overloading

Operators are overloaded using the `op` keyword inside `impl` blocks:

```kobel
pub struct Vec2 {
    pub x: i32,
    pub y: i32
}

impl Vec2 {
    pub op plus(*self, other: Vec2): Vec2 => Vec2(self.x + other.x, self.y + other.y);
    pub op minus(*self, other: Vec2): Vec2 => Vec2(self.x - other.x, self.y - other.y);
    pub op negate(*self): Vec2 => Vec2(-self.x, -self.y);
    pub op equals(*self, other: Vec2): bool => self.x == other.x && self.y == other.y;
    pub op plus_assign(&self, other: Vec2) {
        self.x += other.x;
        self.y += other.y;
    }
}
```

### Supported Operator Names

| Operator Syntax | Method Name | Description |
|---|---|---|
| `a + b` | `plus` | Binary addition |
| `a - b` | `minus` | Binary subtraction |
| `a * b` | `mul` | Binary multiplication |
| `a / b` | `div` | Binary division |
| `a % b` | `rem` | Binary remainder |
| `-a` | `negate` | Unary negation |
| `!a` | `not` | Logical not |
| `a == b` | `equals` | Equality check |
| `a < b`, `a > b` | `compare` | Ordering comparison |
| `a[i]` | `get` | Indexed read |
| `a[i] = v` | `set` | Indexed write |
| `x in seq` | `contains` | Containment test |
| `a += b` | `plus_assign` | In-place addition |
| `a -= b` | `minus_assign` | In-place subtraction |
| `a *= b` | `mul_assign` | In-place multiplication |
| `a /= b` | `div_assign` | In-place division |
| `a %= b` | `rem_assign` | In-place remainder |
| `val/var (a, b) = x` | `unpack` | Destructuring declaration |

---

## 8. Control Flow

### Destructuring Declarations

Kobel supports destructuring via the `op unpack` operator:

```kobel
val (k, v) = entry;
var (x, y) = point;

// In for-loops:
for ((k, v) in hashmap) { ... }
for (val (k, v) in &hashmap) { ... }
```

A struct implements destructuring by defining `pub op unpack`:
- **Out-pointer form** (supports heterogeneous element types):
  ```kobel
  pub op unpack(*self, k: &K, v: &V) {
      k.write(self.key);
      v.write(self.value);
  }
  ```
- **Indexed form** (homogeneous element type):
  ```kobel
  pub op unpack(*self, idx: usz): i32 {
      if (idx == 0) return self.x;
      return self.y;
  }
  ```

### If Expressions and Statements

```kobel
val max = if (a > b) a else b;
```

### Loops

```kobel
// While loop
while (condition) {
    if (should_stop) break;
    if (should_skip) continue;
}

// Ranges
for (i in 0..10) { ... }       // Closed: 0 through 10
for (i in 0..<10) { ... }      // Half-open: 0 through 9
for (i in 0>..<10) { ... }     // Open: 1 through 9
for (i in 10..0) { ... }       // Reverse counting: 10 down to 0
for (c in 'a'..'z') { ... }    // Character ranges

// Iteration over collections and range variables (types implementing Iterable)
val r = 0..10;
for (item in r) { ... }
for (item in list) { ... }
```

### Ranges and Containment

Ranges are first-class values represented by `Range<T>` (defined in `std.range`):
- `..` (closed range, inclusive end bound)
- `..<` (half-open range, exclusive end bound)

Ranges support integers (`i32`, `i64`, `usz`, etc.) and `char`:
```kobel
val r1 = 0..10;          // Range<i32>
val r2 = 0..<len;        // Range<usz>
val rc = 'a'..'z';       // Range<char>

// Containment test with in / !in
if (x in 0..10) { ... }
if (c in 'a'..'z') { ... }
if (x in r1) { ... }
if (x !in r1) { ... }

// Candidate-list containment (zero-allocation inline value, range, and enum tests)
if (c in ['t', 'g', 'k']) { ... }
if (c in ['a'..'z', 'A'..'Z', '_']) { ... }
if (status !in [.OK, .NOT_FOUND]) { ... }
if (shape in [Shape.Circle, Shape.Empty]) { ... }
```

Candidate lists `[a, b, ...]` are valid strictly on the right-hand side of `in` / `!in` (never as standalone array values) and lower directly to short-circuit comparisons without allocating memory. They support primitives, strings, ranges (`..`, `..<`), structs, and enum variants (`Enum.VARIANT`, `.VARIANT`, or bare `VARIANT`, including payload enum tag checks).

When written directly in `for` loop headers (`for (i in 0..10)`, `for (i in 0..<len)`), the compiler lowers them directly to zero-cost C counter loops with automatic bidirectional direction. When assigned to a variable, `Range<T>` implements the `Iterable` trait (`count()`, `at(i)`).

### Pattern Matching (`when`)

`when` works as both an expression and a statement:

```kobel
// Value matching and range patterns
val kind = when (c) {
    'a'..'z', 'A'..'Z' -> "letter";
    '0'..'9'           -> "digit";
    else               -> "other";
};

// Numeric range patterns (including half-open and reverse)
val category = when (score) {
    90..100  -> "A";
    80..<90  -> "B";
    else     -> "C";
};

// Smart cast pattern matching on tagged structs (and grouped type checks)
when (node) {
    is Literal -> return node.value;       // smart cast: node typed as *Literal
    is Binary -> return eval(node.left);
    else -> return 0;
}

val is_valid_node = when (node) {
    is Literal, Binary -> true;            // grouped type pattern: `is A, B`
    else -> false;
};

// Multi-statement block arm with yield
val result = when (action) {
    Action.CALC -> {
        val temp = compute();
        yield temp * 2;
    };
    else -> 0;
};

// Implicit receiver (leading-dot) predicate patterns mixed with value patterns
val tag = when (text) {
    .ends_with("a")      -> 1;
    .starts_with("b")    -> 2;
    .index_of("C") == 3  -> 3;
    "D"                  -> 4;
    else                 -> 0;
};
```

- **Pure subject requirement:** The subject expression inside `when (subject)` cannot contain function or method calls (bind call results to a `val` variable first so the subject is not re-evaluated across arms).
- **Leading-dot patterns (`.member` / `.method()`):** Inside `when (subject)`, a leading `.` or `?.` accesses a field or method on `subject`. Any arm pattern using a leading-dot receiver must evaluate to `bool` and acts as a predicate test.

### Statements and Increment

```kobel
var count = 0;
count++;       // Statement increment
count--;       // Statement decrement
count += 5;    // Compound assignment
```

### Defer

The `defer` statement schedules cleanup code to execute at scope exit:

```kobel
var arena = Arena(1024);
defer arena.free();
```

---

## 9. Modules and Visibility

```kobel
mod my_project.utils.helpers;

use std.io.*;
use std.collections.List;
use std.collections.HashMap as Map;
```

- **Module path:** Matches the file path. Segments must be valid identifiers.
- **Imports:** Wildcard `use a.b.*` or explicit `use a.b.Symbol`. Import aliasing `use a.b as c` is supported.
- **Visibility:** `pub` exports structs, functions, methods, fields, and constants. Symbols without `pub` are private to their defining module.

---

## 10. Standard Library Highlights

Standard library modules live in `lib/std/`:

- `std.io`: Console I/O (`print`, `println`).
- `std.filesystem.File`: Static filesystem operations (`File.open`, `File.read`, `File.write`, `File.append`, `File.exists`, `File.remove`, `File.list_dir`).
- `std.filesystem.Path`: Path manipulation utilities.
- `std.collections`: Generic dynamic containers:
  - `List<T>`: Dynamic resizable array.
  - `HashMap<K: Hash, V>`: Hash table.
  - `HashSet<T: Hash>`: Set collection.
  - `StringBuilder`: Efficient string assembly.
- `std.memory`: Memory allocation interfaces and implementations (`Allocator` trait, `Heap`, `Arena`).
  - `create<T>`, `create_val<T>`, `alloc<T>`, `resize<T>`, `free<T>`.
  - Bump allocator (`Arena`) for single-point bulk deallocation.
- `std.traits`: Core traits (`Iterable`, `Hash`, `Stringer`).
- `std.sys`: System helpers (`exec`, `sys_exit`, `is_windows`, `has_env`).

---

## 11. Foreign Function Interface (FFI)

```kobel
extern "libc" {
    fn printf(fmt: *char, ...): i32;
    fn malloc(size: usz): *u8;
    fn free(ptr: *u8);
}

pub extern fn add(a: i32, b: i32): i32 {
    return a + b;
}
```

- `extern "libc"` blocks declare external C bindings. Variadic arguments (`...`) are supported for C calls.
- Struct memory layout in Kobel is C99-compatible by default.

---

## 12. Program Entry Point

```kobel
pub fn main() {
    println("Hello, World!");
}

// Or returning an exit code:
pub fn main(): i32 {
    return 0;
}
```

---

## 13. Future Plans

The following features are designed for future language versions:

- **Error Union Types & Faults:** A planned error handling model with `fault` definitions and recovery operators (`!:`) to replace plain status returns.
- **Lambdas and Closures:** Inline anonymous functions and closures with variable capture.
- **Freestanding Target:** Direct syscall / no-libc compilation mode.
- **Concurrency:** Structured concurrency and lightweight threading abstractions.
