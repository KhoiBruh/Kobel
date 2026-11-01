# Kobel Language Specification

> Source of truth for Kobel syntax and semantics. The self-hosted compiler in `src/` is the reference implementation; where this document and the compiler disagree, file an issue.

## 1. Overview

- **Kind:** general-purpose, procedural with object-oriented features. No `class`: `struct` holds data, `impl` holds behavior, `trait` provides abstraction. All dispatch is resolved statically at compile time — no virtual tables.
- **Compilation:** Kobel source → C99 → native binary via a C compiler (`cl` by default).
- **Memory:** no GC. Every value has a single owner and is released at scope end. The compiler checks moves only (no full borrow checker); overlapping borrows are the programmer's responsibility.
- **Attributes:** `@operator(...)`, `@infix`. Users cannot define new attributes — the set is fixed by the language.
- **Error model:** no exceptions. Errors are values: functions return union types with `fault`, handled with `!:`; nulls are handled with `?.` / `?:`.

## 2. Lexical structure

- Blocks use `{ ... }`.
- Line comment: `// ...`. Block comment: `/* ... */`.
- Keywords: `fn`, `struct`, `trait`, `fault`, `enum`, `alias`, `const`, `val`, `var`, `mod`, `use`, `pub`, `extern`, `defer`, `as`, `as?`, `in`, `if`, `else`, `when`, `is`, `for`, `while`, `loop`, `break`, `continue`, `return`, `yield`, `self`, `Self`, `null`, `true`, `false`, `and`, `or`, `not`.
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`. Module segments must be identifiers, so keywords cannot be path segments.

### Numeric literals

Hex (`0xFF`), binary (`0b1010`), and `_` separators (`1_000_000`) are supported. Type suffixes are **UPPERCASE**; default is `i32` for integers, `f64` for floats:

| Suffix | Type |
|---|---|
| *(none, no `.`)* | `i32` (or context-inferred, see below) |
| `B` / `S` / `L` / `Z` | `i8` / `i16` / `i64` / `isz` |
| `U` / `US` / `UL` / `UB` / `UZ` | `u32` / `u16` / `u64` / `u8` / `usz` |
| *(none, with `.`)* or `D` | `f64` |
| `F` | `f32` |

**Contextual inference:** an unsuffixed integer literal expression (literals joined by `+ - * / %`, parentheses allowed) takes the surrounding type instead of defaulting to `i32`:

```kobel
val a = i32.size() * 2;   // 2 infers to usz
var c: usz = 0;
c = c + 1;                // 1 infers to usz
```

Rules: an explicit suffix always wins; mixed-type operands of non-literal values still require `as`; a negative literal never infers to an unsigned type.

## 3. Primitive types

| Group | Types |
|---|---|
| Signed integers | `i8`, `i16`, `i32`, `i64`, `isz` |
| Unsigned integers | `u8`, `u16`, `u32`, `u64`, `usz` |
| Floats | `f32`, `f64` |
| Boolean | `bool` |
| Character | `char` (1-byte ASCII) |
| String | `str` (heap-allocated, owned) |
| Unit | no keyword — a function without a declared return type returns unit |

`isz`/`usz` are pointer-width integers. `str` is length-tagged, not null-terminated (see §12 for C interop).

### String interpolation

Inside `"..."`, `${` … `}` opens a hole holding any expression, evaluated exactly once, in position:

```kobel
val name = "Kobel";
println("Hello ${name}, n = ${n}");      // str, char, bool, numbers interpolate
println("${obj.describe()} | ${list.len}");
println("price: \$5");                   // `\$` is a literal `$`
```

Outside a hole, `{` and `}` are plain characters. There is no `$name` shorthand — always write `${name}`. Interpolable types: `str`, `char`, `bool`, all integer types (enums included, as their numeric value), `f32`/`f64`. Anything else is a compile error. Lowered in sema to `kobel_concat` calls; codegen is untouched.

### Adjacent string concatenation

Adjacent string literals (separated only by whitespace/newlines) merge at compile time, including interpolated ones:

```kobel
val query = "SELECT id, name, email "
            "FROM users "
            "WHERE active = 1;";
```

### Nullable types and raw pointers

```kobel
val a: i32? = null;
```

- `T?` is nullable; use `?.` for safe navigation and `?:` (Elvis) for the fallback (see §10).
- Raw pointers work everywhere, with no `unsafe` block: `*T` is read-only, `&T` is read/write.

### Casts

```kobel
val b = a as u32;    // panics if out of range or mismatched
val c = a as? u32;   // returns nullable instead of panicking
```

Widening casts always succeed. There is no unchecked raw cast.

## 4. Composite types

| Type | Meaning | Storage |
|---|---|---|
| `Array<T>` | Fixed size, known at compile time | Stack-like |
| `List<T>` | Dynamic, self-resizing (`use std.collections.list.*`) | Heap |
| `HashMap<K, V>` / `HashSet<T>` | Hash-based (`use std.collections.hash_map.*`) | Heap |

```kobel
val a: Array<i32> = [0, 1, 2, 3];            // size inferred = 4
val b: Array<i32>(4) = [0, 1, 2, 3];         // explicit size must match exactly
var m: HashMap<str, i32> = HashMap<str, i32>();
```

`Array` and `str` are in the prelude; everything else must be imported.

### Self-referential structs

```kobel
struct Node {
    value: i32,
    next: *Node?
}
```

Linked structures use nullable raw pointers with manual allocation and `defer` for cleanup. There is no owning smart pointer yet (see §14).

### Forward declarations

```kobel
struct A;                 // 'A' usable from here, behind *A only

struct B {
    pub a: *A
}

struct A {                // completes the same type 'A'
    pub v: i32,
    pub b: *B
}
```

A forward declaration registers an incomplete type: usable behind a pointer, rejected by value. Declaration and definition are one type; the backend emits a single typedef.

## 5. Structs, traits, faults, enums

### Structs and `impl`

```kobel
struct A {
    pub a: i32,
    pub b: str
}

impl A {
    fn new(c: i32 = 1, d: str = "hello"): Self {
        return Self(c, d);
    }
    // or short form:
    fn new(c: i32 = 1, d: str = "hello") => Self(c, d);
}
```

- Structs declare fields only; methods live in `impl` blocks. Fields support per-field `pub`.
- Without `new(...)`: construct positionally with every field in order, `A(1, "hello")`. With `new(...)`: construct per its signature. `Self(...)` always refers to the enclosing struct.
- Memory layout is always C-compatible (declaration order, natural alignment) — no marker needed, FFI-ready.
- Any function may use the single-expression body form `=> expr;`.

### Traits

```kobel
trait B {
    fn greet(val self): str => "hello";   // default implementation
}

impl B for A {
    fn greet(val self): str => "hi, " + self.b;
}
```

Traits may provide default implementations, be implemented many-per-struct, and inherit from other traits.

### Associated types

```kobel
trait Container {
    type Item;
    fn first(val self): Self.Item;
}

impl Container for IntBox {
    type Item = i32;
    fn first(val self): Self.Item => self.v;
}
```

- Declare with `type Name;` in the trait, bind with `type Name = Type;` in the impl, use as `Self.Name`.
- Every impl must bind all associated types exactly once, with no stray names.
- Generic impls bind per instance (`impl Container for Box<T> { type Item = T; }`).

### Faults (error types)

```kobel
fault A;                     // carries no data
fault A(message: str);       // carries data, struct-like
fault B : A;                 // extension — organizational only
```

Faults are not real types: no traits apply. `:` is **not** subtyping — a thrown `B` is caught only by a `B` arm, never implicitly as `A`.

### Plain enums

```kobel
enum Status : u16 {   // `: u16` may be omitted; the compiler picks the layout
    A,        // 0
    B = 504,
    C,        // 505 (auto-increment)
    D,        // 506
}

Status.A.value      // the integer value
Status.of(505)      // Status? — nullable, not a fault
```

A plain enum is a named integer type usable in arithmetic and comparison.

### Payload enums

```kobel
enum Shape {
    Circle(radius: f64),
    Rect(width: f64, height: f64),
    Empty
}

val c = Shape.Circle(2.0);
val e = Shape.Empty;              // empty variant used directly

val a = when (c) {
    is Shape.Circle as circle -> 3.14 * circle.radius * circle.radius,
    is Shape.Rect as r -> r.width * r.height,
    is Shape.Empty -> 0.0,
    else -> 0.0
};
```

- Variants carry named fields. `is Shape.Circle as x` binds `x` as a **pointer** to the payload, so `x.radius` works for both value and pointer subjects.
- C layout: `struct Shape { int32_t tag; union { ... } u; }`; each variant is a struct type (`Shape__Circle`) with a tag constant (`Shape__Circle__TAG`).
- Not yet supported: generic enums, explicit discriminants, or custom backing types on payload enums.

### `alias`

```kobel
alias Number = Comparable + Cloneable;   // combined trait bound for <T: Number>
alias Meters = f64;                       // plain type alias
```

## 6. Ownership and memory

### Three parameter modes

| Declaration | Meaning | Takes ownership? |
|---|---|---|
| `self: T` | Move | Yes |
| `val self: T` | Read borrow | No |
| `var self: T` | Read/write borrow | No |

The compiler checks moves only — no borrow checker or lifetimes. Overlapping borrows cannot cause memory unsafety, only logic bugs.

### `val` / `var` / `const`

- Local `val`/`var` controls **rebinding only**, independent of ownership. A moved-from `val` is dead for good; a moved-from `var` can be reassigned and reused. A `val` binding may still be passed to `var self` or owning parameters — that does not rebind it.
- `const` is top-level only. Its value must be compile-time computable (no function calls).
- Structs without a cleanup function require manual `defer <cleanup>(...)`; structs with one clean up automatically at scope end.

## 7. Generics

```kobel
fn max<T: Comparable>(val a: T, val b: T): T { ... }
struct Box<T: Comparable> { value: T }
```

Applies to functions and structs. Multiple bounds via `+` (`T: A + B`) or a pre-combined `alias`. No `where` clause. Instantiation is monomorphization through the `sema/mono` worklist (see `docs/sema.md`).

## 8. Control flow

```kobel
val a = if (x > 0) 1 else -1;   // if/else is an expression

for (i in 0..10) { ... }        // closed–closed
for (i in 0>..<10) { ... }      // open–open
for (i in 0..<10) { ... }       // half-open, excludes the end
for (i in 10..0) { ... }        // reversed, closed–closed
for (i in iterator) { ... }
for ((k, v) in map) { ... }     // destructuring

while (cond) { ... }
loop { ... break; }             // no do-while

val result = when (x) {
    1 -> "one",
    2, 3 -> "two or three",
    else -> "other"
};
```

- All three range forms pick direction at runtime from the written bound order (`10..0` counts down). The loop variable is a read-only copy scoped to the body; `continue` still advances it. `0..<seq.len` is the indexing idiom — correct even when empty.
- `break`/`continue` support labels for nested loops (exact label syntax still open, see §14).

### Pattern matching and smart casts

```kobel
when (node) {
    is Literal as lit -> lit.value;             // read-only bind, 'lit: *Literal'
    is Binary as var b -> {                     // mutable bind ('as &b' also works)
        b.left = new_node;
        yield eval(b.left) + eval(b.right);
    }
    else -> 0;
};

if (node is Literal as var lit) {
    lit.value = 42;
}
```

Matching is on a leading tag field: `is X` compares `subject.<first field>` against `X.TAG`. Participating structs must declare the tag field **first** and a `const TAG`. Binding forms: `as x` (read-only pointer), `as val x` (same, explicit), `as var x` / `as &x` (mutable). Destructuring: `val (a, b) = getPair();`.

## 9. Operators and overloading

```kobel
impl A {
    @operator(+)
    fn plus(val self, other: A): A => A(self.a + other.a, self.c + other.c);
}
```

- No operator traits — `@operator(...)` decides dispatch, the function name is free. Arithmetic and comparison operators supported; wrong types or missing impls are compile errors.
- Custom infix functions:

```kobel
@infix
fn to(val self, other: B): Pair<A, B> => Pair(self, other);

val p = a to b;   // desugars to a.to(b)
```

`in` is hard syntax, not an infix function.

## 10. Error handling and null-safety

```kobel
fn parse(s: str): i32 | IllegalArguments {
    if (invalid) { return IllegalArguments(...); }
    return 5;
}

val a = parse(s) !: 0;                            // fallback value
val b = parse(s) !: return IllegalArguments(...); // early return
val c = parse(s) !: when {                        // discriminate by fault type
    IllegalArguments -> ...,
};

val d = maybeNull() ?: 0;   // Elvis for null

val e = parse(s);   // unhandled -> panic with full diagnostics
```

- `!:` handles faults: fallback value, early return, or a `when` over fault types. Unhandled fault values panic.
- `?.` is safe navigation; `?:` (Elvis) supplies the null fallback. Safe-nav on a non-pointer must pair with `?:`; comparisons in conditions must short-circuit.

## 11. Modules

```kobel
mod a.b.c;   // c = file name, a.b = directory path

use a.b.c.A;
use b.c.d.A as B;
use b.c.d.*;     // pulls in only what is actually used

use a.b.B;
use a.c.B;       // clash -> refer via trailing module: b.B and c.B
```

- If the trailing modules also clash, `use ... as <alias>` is mandatory. Fully-qualified paths as a disambiguator are not allowed.
- Visibility is `pub` only — no `protected`/`internal`.
- **Cyclic imports are allowed** between modules, but cross-references must go through a **pointer** (`struct A { b: *B }`); by-value cycles are rejected (C cannot hold them either). Type names are registered before field resolution, so declaration order across modules does not matter.

## 12. FFI

```kobel
extern "libc" {
    fn printf(fmt: *char, ...): i32;
    fn malloc(size: usz): *u8;
}

pub extern fn compute(x: i32): i32 {
    return x * 2;
}
```

- Variadic parameters (`...`) are supported when **calling into** C; Kobel itself defines no variadic functions.
- `str` is not C's `*char` (length-tagged, not null-terminated) — convert via stdlib helpers.
- Exported functions (`pub extern fn`) must have a body and **cannot be overloaded** (C ABI needs one symbol name).
- Struct layout is C-compatible by default (§5), so FFI needs no extra marking.

## 13. Program entry

```kobel
pub fn main() { ... }
```

A command-line-argument variant is planned; its exact signature is still open (see §14).

## 14. Open issues

- Lambda/closures (Kotlin-style `list.filter { ... }` proposed; capture semantics undecided).
- Freestanding / no-libc mode (direct syscalls, custom `_start`) — deferred.
- `Box<T>` owning smart pointer for self-referential structs.
- Concurrency model (raw libc threads vs dedicated syntax).
- Per-field `pub` granularity beyond current support.
- `///` doc-comment generation.
- Package manager / build system for multi-file projects.
- Exact `break`/`continue` label syntax.
- Exact `main(args: ...)` signature.
