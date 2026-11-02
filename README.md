# Kobel

A small, self-hosted systems language. The compiler is written in Kobel itself, emits C99, and links with any C compiler (`cl` by default).

```
.kb source → Kobel v1 → C99 → native binary
```

## Features

- **Structs, enums, traits** — data with `struct`, behavior with `impl`, static-dispatched abstraction with `trait`. No classes, no virtual tables.
- **Generics with monomorphization** — generic structs, functions, and methods, specialized at compile time.
- **Pattern matching** — `when` expressions over enums, payloads, and ranges.
- **Null-safety** — safe navigation (`?.`) paired with Elvis (`?:`); short-circuiting conditions, no manual null-check clutter.
- **Expressive strings** — `${...}` interpolation with arbitrary expressions, adjacent-literal concatenation at compile time.
- **Modules** — file-path-based modules (`mod a.b.c`), glob and aliased imports, `pub`-only visibility.
- **C FFI** — `extern "libc"` blocks, variadic calls, C-compatible struct layout.
- **No GC** — single-owner values freed at scope end; move-checked by the compiler.
- **Batteries included** — stdlib (`io`, `str`, `collections`, `mem/arena`, `traits`), plus `fmt`, LSP, and test/snapshot tooling.

## Quickstart

```bat
scripts\build_seed.bat               :: compile dist/bootstrap.c → build\seed\kobel_seed.exe
.\build\seed\kobel_seed.exe build    :: self-compile → build\kobel.exe
.\build\kobel.exe hello.kb -o hello  :: compile a program
powershell -File scripts/check.ps1   :: full gate: build → test → snap → fixpoint
```

```kobel
use std.io.*;

pub fn main() {
    val name = "Kobel";
    println("Hello, ${name}!");
}
```

## Roadmap

- [x] Self-hosted compiler (Kobel → C99 → native)
- [x] Generics, traits, pattern matching, null-safety operators
- [x] Bootstrap fixpoint: v2 emits byte-identical C vs v1
- [x] Bootstrap purely from generated C seed (dist/bootstrap.c)
- [ ] Error union types / faults
- [ ] Lambda / closures
- [ ] Freestanding (no-libc) target
- [ ] Concurrency model
- [ ] Package manager / build system

## Docs

- `docs/spec.md` — language specification (source of truth)
- `docs/architecture.md` — compiler architecture and bootstrap plan
- `docs/sema.md` — semantic-analysis pass map
- `AGENTS.md` — contributor and agent guide (build, layout, code standards)
