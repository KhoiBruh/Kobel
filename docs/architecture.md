# Kobel Compiler Architecture

This document describes the current architecture of the self-hosted Kobel compiler, its 4-stage pipeline, bootstrap lifecycle, and core engineering invariants.

For the language grammar and semantics, see [`spec.md`](spec.md). For semantic analysis details, see [`sema.md`](sema.md). For generic monomorphization mechanics, see [`parameterised-ir.md`](parameterised-ir.md).

---

## 1. Overview

Kobel is a self-hosted systems programming language. The reference compiler is written in Kobel itself (`src/`), compiles source code into clean C99, and calls the host C compiler (`cl` by default on Windows, `clang`/`gcc` on POSIX) to produce native binaries.

```
.kb source files
       │
       ▼
Kobel Compiler (v1)
       │
       ▼
      C99
       │
       ▼  (Host C Compiler: cl / gcc / clang)
Native Executable (.exe)
```

---

## 2. Bootstrap Lifecycle & Fixpoint

The bootstrap foundation is a single standalone C99 file: `dist/bootstrap.c`. No external LLVM or C++ toolchains are required.

```
dist/bootstrap.c ──(cl / C compiler)──▶ build/seed/kobel_seed.exe
kobel_seed.exe <src> ──(compile)──▶ build/kobel.exe (v1)
build/kobel.exe <src> ──(self-compile)──▶ build/kobel.exe (v2)
  └─ byte-identical C99 output vs v1 ──▶ FIXPOINT ACHIEVED
```

### Bootstrap Commands

```bat
:: 1. Build the seed compiler from dist/bootstrap.c
scripts\build_seed.bat

:: 2. Use the seed to build the v1 compiler
.\build\seed\kobel_seed.exe build

:: 3. Run full verification gate (build -> test -> snap -> fixpoint)
powershell -ExecutionPolicy Bypass -File scripts/check.ps1

:: 4. Update seed after making compiler changes
build\kobel.exe src/main.kb -emit-c dist/bootstrap.c
```

---

## 3. The 4-Stage Compiler Pipeline

The compilation pipeline is executed sequentially in `src/compiler/driver/pipeline.kb`:

| Stage | Main Function | Key Modules | Responsibility |
|---|---|---|---|
| **[1/4] Load** | `load_program` | `loader`, `lexer`, `parser`, `ast` | Recursively resolves modules from `use` statements. Lexes and parses each file into an AST. |
| **[2/4] Collect** | `collect_program` | `sema.decl`, `sema.mono` | Collects top-level declarations, registers generic templates, resolves type annotations, and monomorphizes generic structs and functions. |
| **[3/4] Check & Lower** | `check_program`, `lower_program` | `sema.body`, `sema.lower`, `sema.desugar` | Typechecks function and method bodies using side-table annotations, resolves generic methods, and lowers language sugar (`for` loops, string interpolation, operator calls) into C-emittable AST nodes. |
| **[4/4] Codegen** | `gen_program` | `codegen` | Emits C99 source code. Invokes the backend C compiler unless `-emit-c` is passed. |

### Module Search Roots

Module paths map directly to filesystem paths (`compiler.sema.body.pass` → `src/compiler/sema/body/pass.kb`). The loader searches:
1. Directory of the input file.
2. Extra roots specified via `-I <dir>`.
3. Standard library root: `lib/`.
4. Project root: `src/`.

---

## 4. Compiler CLI

The unified `kobel` executable (`build/kobel.exe`) supports project commands and direct file compilation:

```
kobel <command> [args]

Commands:
  build             Build project defined in kobel.toml
  run [args...]     Build and run project (rebuilds only when source changes)
  test [filter]     Run test suite in tests/ (with optional name filter)
  snap [--update]   Compare emitted C against golden snapshots in tests/golden/
  fixpoint          Verify self-hosting byte identity
  emit-c [file]     Emit generated C99 source (default: dist/bootstrap.c)
  fmt [-w] <files>  Format Kobel source code
  lsp               Launch the Language Server Protocol daemon
  version           Display compiler version

Direct compilation:
  kobel <source.kb> [-o <out.exe>] [-emit-c <out.c>] [--cc cl|gcc|clang] [-I <dir>]
```

---

## 5. Source Tree Layout

```
Kobel/
├── src/
│   ├── main.kb                       # Compiler entry point and CLI router
│   ├── compiler/
│   │   ├── ast/                      # AST node definitions and Arena builders
│   │   ├── lexer/                    # Token types and scanner
│   │   ├── parser/                   # Recursive descent parser
│   │   ├── sema/                     # Semantic analysis passes & type system
│   │   │   ├── body/                 # Function body checking & expression typing
│   │   │   ├── decl/                 # Declaration pass & type resolution
│   │   │   ├── mono/                 # Generic monomorphization registry & worklist
│   │   │   ├── desugar/              # Syntactic desugaring (loops, interpolation)
│   │   │   └── types.kb, symbol.kb   # Core type store, layout, and symbol tables
│   │   ├── codegen/                  # C99 code emitter & C runtime prelude
│   │   └── driver/                   # Pipeline driver, options, and commands
│   ├── config/                       # TOML configuration parser
│   ├── fmt/                          # Code formatter
│   ├── lsp/                          # Language Server Protocol implementation
│   └── util/                         # Common string and path utilities
├── lib/
│   └── std/                          # Standard library (io, sys, collections, mem, etc.)
├── tests/
│   ├── features/                     # Language feature test suite
│   ├── golden/                       # Reference C99 output digest snapshots
│   ├── stdlib/                       # Standard library tests
│   └── unit/                         # Compiler unit tests
├── dist/
│   └── bootstrap.c                   # C99 bootstrap seed
├── scripts/
│   ├── build_seed.bat                # Compiles dist/bootstrap.c using MSVC
│   └── check.ps1                     # CI quality gate runner
├── kobel.toml                        # Project build configuration
└── AGENTS.md                         # Coding conventions and development rules
```

---

## 6. Core Engineering Invariants

1. **AST Immutability (I1):** AST nodes parsed during Stage 1 remain structurally immutable. Per-instance type information, method resolutions, and identifier mangling are recorded in semantic side tables (`InstanceRecord.ann_map`). High-level nodes are never mutated in place; transformed nodes are substituted via `resolved_node`.
2. **Codegen Type-Blindness (I2):** The C99 code generator performs zero type inference. It strictly reads `node.type_ptr` and `node.resolved_*` annotations produced by semantic analysis. If an expression lacks a resolved type, semantic analysis has failed.
3. **One-Way Module Dependencies:** The dependency graph is strictly acyclic:
   `std.*` → `compiler.ast` → `compiler.sema` → `compiler.codegen` → `compiler.driver` → `main`.
   The sole exception is a pointer-only cycle within `sema/types.kb` ↔ `methods/structs/enums`, where cross-references are strictly constrained to raw pointers (`*T` or `List<*T>`).
4. **Byte-Identical Fixpoint:** Any valid refactoring of compiler passes must preserve byte-identical C output when the compiler compiles itself.
