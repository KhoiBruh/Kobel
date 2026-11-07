# AGENTS.md — Kobel

Self-hosted Kobel language. The real compiler is written in Kobel (`src/`), emits C99, then invokes a C compiler (`cl` by default).

## Bootstrap lifecycle (must understand)

```
v0 (C++/LLVM, branch v0-cpp, frozen) ──▶ kobel_v1.exe
kobel_v1.exe <src> ──▶ self-compile ──▶ C99 ──▶ kobel_seed.exe
kobel_seed.exe <src> ──▶ byte-identical C99 output vs v1 → FIXPOINT
```

End goal: drop v0/LLVM entirely, use v1's own C output as the seed (`docs/architecture.md` §9).

## Build / check commands

```bat
scripts\build_seed.bat                        :: cl dist/bootstrap.c → build\seed\kobel_seed.exe
.\build\seed\kobel_seed.exe build             :: build v1 from src/main.kb → build/kobel.exe
powershell -File scripts/check.ps1            :: standard gate: build → test → snap → fixpoint
powershell -File scripts/check.ps1 -Update    :: accept current C output (snap --update)
```

v1 compiler commands (`build/kobel.exe`):

```
kobel build                  :: build per kobel.toml ([build] entry/output/cc)
kobel test [filter]          :: run tests/ (dir configured in [test])
kobel snap [--update]        :: compare C output against tests/golden/
kobel fixpoint               :: verify v2 emits byte-identical C vs v1
kobel <source.kb> [-o exe] [-emit-c f.c] [--cc cl|clang|gcc] [-I dir]
```

After every compiler source change: regenerate the seed and commit golden files if the C diff is intentional:

```
build/kobel.exe src/main.kb -emit-c dist/bootstrap.c
```

`build/`, `dist/*` (except `dist/bootstrap.c`), `*.exe`, `*.obj` are all gitignored.

## Source layout

```
src/main.kb                  :: entry
src/compiler/loader|lexer|parser|ast|semantic|codegen|driver
src/compiler/semantic/{types,symbols,resolution,declaration,checker,lowering}
src/compiler/codegen/{generator,program,statement,expression,types,runtime}
src/{config,fmt,lsp,util}    :: toml/project, formatter, LSP, strutil
lib/std/                     :: stdlib (io, sys, ascii, str, fmt, collections/*, mem/*, traits/*)
tests/{features,golden,stdlib,unit}
docs/{architecture,sema,parameterised-ir}.md :: architecture + sema map
docs/spec.md           :: language spec (source of truth for syntax/semantics)
```

v1 pipeline has exactly 4 steps (`src/compiler/driver/pipeline.kb`): `load_program` → `collect_program` (DeclarationCollector) → `check_program` (BodyChecker) + `lower_program` (LoweringPass) → `gen_program` (CCodeGen emit C99).

## Hard conventions

- **Module path = file path**: `semantic/checker/statement.kb` → `compiler.semantic.checker`. Segments must be IDENTIFIERs, so avoid keywords.
- **One-way dependencies, strictly acyclic.** Zero dependency cycles across subsystems.
- **Full dependency graph**: see `docs/architecture.md` §3.3. Any new import must keep the graph acyclic.
- **codegen is read-only**: it reads AST + `type_ptr`/`resolved_*` only, never infers types. All type resolution lives in `compiler.semantic.*`.
- **Null-safety**: use safe navigation `?.` + Elvis `?:` instead of manual null checks; safe-nav on a non-pointer must be paired with `?:`; comparisons in conditions must short-circuit. See commits `38384ab`, `0d8ee3d`.
- **Generics**: templates registered in `semantic.declaration`, instantiated via `semantic.resolution` (registry + worklist + instantiate + clone + rewrite), generic methods flushed late in `semantic.checker.program`.
- **Surgical edits**: touch only what the request requires; match existing style; no drive-by refactors, no unrelated dead-code removal.
- **Commits**: `check.ps1` gate must be green before committing; message names the scope (`feat/fix/refactor(semantic|codegen|...)`).

## Code standards (Kobel `.kb`)

Enforceable rules with correct / incorrect examples. All code and docs are 100% English.

### 1. File layout

Every file starts with `mod`, then `use` imports grouped and ordered: `std.*` → `compiler.*`/`util.*` → specific symbol imports. Non-trivial modules carry a `// ===...` banner describing ownership.

```kobel
mod compiler.semantic.resolution.registry;

use std.collections.list.*;
use std.memory.Arena;

use compiler.ast.node.*;
// ... wildcard group imports before single-symbol imports ...

use compiler.semantic.types.DataType;
use compiler.semantic.types.Type;
```

- One concept per file; file name = snake_case of the main type (`resolution/registry.kb` → registry/worklist).
- Never add an import that creates a dependency cycle (see Hard conventions).

### 2. Formatting

- Tabs for indentation, K&R braces (opening brace on the same line), max width 100 (see `FormatOptions` in `src/fmt/options.kb`).
- Braces may be omitted for short single-line bodies (`if (cond) return x;`), or when a multi-line condition closes with `) return x;` on the closing line. Multi-line bodies always require braces. One statement per line.

```kobel
// CORRECT
if (node == null) {
    return none_ty;
}

// INCORRECT — missing braces
if (node == null) return none_ty;
```

### 3. Naming

| Entity | Case | Example |
|---|---|---|
| Struct, enum, trait | PascalCase | `StructType`, `TokenType` |
| Function, variable, field, param | snake_case | `collect_program`, `type_size` |
| Enum variant, `const TAG` | UPPER_SNAKE | `POINTER`, `KW_FN` |

- Method receivers are explicit: `val self` (read-only) or `var self` (mutating). Single-expression bodies may use `=>`.

```kobel
impl PointerType {
    pub fn type_size(val self) => 8;
    pub fn ensure(var self, cond: bool, node: *AstNode, msg: str) { ... }
}
```

### 4. `val` by default

Prefer `val`. Use `var` only when the binding is reassigned.

```kobel
// CORRECT
val diag = Diagnostic.parse(err, default_file, loader);
var already_seen = false;

// INCORRECT — never reassigned, must be val
var diag = Diagnostic.parse(err, default_file, loader);
```

### 5. Null-safety (`?.` / `?:`)

Manual null checks are forbidden where `?.` + `?:` applies. Safe-nav on a non-pointer value must be paired with `?:`.

```kobel
// CORRECT
val name = decl?.full_path ?: "";
for (decl in self.declarations) if (decl is ModuleDecl) return decl.full_path;

// INCORRECT — manual null check instead of ?. / ?:
val name = "";
if (decl != null) { name = decl.full_path; }
```

### 6. Type narrowing (`is`)

Narrow and downcast with `is` pattern matching and smart casting. Never force-cast a value whose type is unchecked.

```kobel
// CORRECT
if (decl is ModuleDecl as m) return m.full_path;
if (decl is ModuleDecl) return decl.full_path;
```

### 7. Error handling

- Sema passes report via `report_error(node, msg)` / `ensure(cond, node, msg)`; parsers append to `errors: List<str>`. Never fail silently, never print-and-continue.
- CLI commands return `i32` exit codes (`0` = success); `check.ps1` treats any non-zero exit as a failed gate.
- No `TODO`/`FIXME`/`HACK` markers in `src/` — file issues or fix immediately.

### 8. Comments and docs

- English only, in code and in `docs/`.
- `//` for short single-line notes stating rationale ("why"), never restating the code ("what").
- `/* ... */` for any multi-line note (module banners, ownership docs, multi-sentence rationale). Never stack multiple `//` lines where one block comment fits.

```kobel
// CORRECT — banner and ownership doc as block comments
/*
 * ============================================================================
 * Monomorphization — template registry and instance worklist
 * ...
 * ============================================================================
 */

// CORRECT — short rationale stays on one line
// Standard construction: reuse the declaration pass of the same compilation.
pub init(symtab: SymbolTable, decl: DeclPass): Self { ... }

/* CORRECT — multi-sentence rationale as a block comment */
/*
 * A template is a candidate when its name matches and its table kind agrees.
 * Pointer-only fields keep this cycle safe; see Hard conventions.
 */

// INCORRECT — stacked // lines instead of a block comment
// A template is a candidate when its name matches
// and its table kind agrees.
```
- Public APIs that cross module boundaries get a one-line doc comment stating ownership (who writes the data, who reads it). Keep the sema ownership table in `docs/sema.md` accurate.

### 9. Tests

- Every language/behavior change ships with a test: `tests/features/test_<area>.kb`, plus golden files under `tests/golden/` regenerated via `kobel snap --update` only after reviewing the C diff.
- Name tests `test_<area>.kb`; shared fixtures live in `tests/features/testsupport/`.
- Full gate before commit: `powershell -File scripts/check.ps1` (build → test → snap → fixpoint).
