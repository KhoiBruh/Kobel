# Kobel Compiler Architecture Refactoring Plan

This document is the authoritative roadmap and technical specification for the ground-up architectural refactoring of the self-hosted Kobel compiler on branch `refactor/compiler-architecture`.

It is designed so that any developer or AI agent can pick up work in a fresh session without needing prior conversation history.

---

## 1. Core Architectural Principles

1. **Linux-Kernel-Style Subsystem Isolation**:
   - Every compiler subsystem lives in its own directory and represents a single, well-defined module (`mod compiler.<subsystem>;` or `mod compiler.semantic.<subsystem>;`).
   - **Zero `mod` path lies**: Every `.kb` file in directory `src/a/b/c/` MUST declare `mod a.b.c;`. Never declare a parent or sibling module path to attach methods across directory boundaries.
2. **Strict One-Way Dependency DAG**:
   - Subsystems depend strictly from bottom to top. Circular module imports are forbidden.
   - High-level pipeline DAG:
     `std.*` → `compiler.core` → `compiler.lexer` → `compiler.ast` → `compiler.parser` → `compiler.loader` → `compiler.semantic.*` → `compiler.codegen` → `compiler.driver` → `main`.
3. **Purely Syntactic AST & External Semantic Side-Tables**:
   - `AstNode` (`compiler.ast`) is 100% syntactic (`id: NodeId`, `data: NodeData`, `span: Span`).
   - No `*u8` untyped pointers, no forward struct declarations, and no C-codegen fields live on `AstNode`.
   - All semantic decisions (resolved types, resolved methods, lowered AST replacements, generic instance records, `is`-pattern lowering metadata) live in `SemanticTable`.
4. **Full, Non-Abbreviated Naming**:
   - Module and file names use clear, full English nouns (`declaration.kb`, `statement.kb`, `expression.kb`, `semantic/`, `resolution/`, `lowering/`) instead of cryptic abbreviations (`decl.kb`, `stmt.kb`, `expr.kb`, `sema/`, `mono/`).
5. **Formatting Convention**:
   - Tabs for indentation, K&R braces, max line width 100.
   - Short single-line `if (cond) return x;` statements may omit braces.
   - Multi-line `if` conditions formatted with leading `||` / `&&` per line and closing with `) return x;` on the final line are permitted without braces.

---

## 2. Completed Phases (Phases 1–5)

| Phase | Subsystem | Commits | Summary of Completed Work |
|---|---|---|---|
| **Phase 1** | `compiler.core` & `std.string` | `acf4fce`, `0cbc49e`, `7de1d9b` | Created `src/compiler/core/{source,interning,diagnostic,naming}.kb` (`Span`, `SourceMap`, `InternPool`, `DiagnosticBag`, identifier mangling). Added `str(c_str)`, `str.blank()`, `str.split()`, `str.trim_indent()`, `str.multiline_trim_indent()` to `lib/std/string/str.kb`. Deleted `src/util/`. |
| **Phase 2** | `compiler.lexer` | `c1a6dcf` | Rebuilt `src/compiler/lexer/{token,lexer}.kb` to attach `Span` to every `Token`, track exact multiline token start positions, provide token classification methods, and record lexical errors. |
| **Phase 3** | `compiler.ast` | `0d72d13` | Rebuilt `src/compiler/ast/{node,declaration,statement,expression,types}.kb`. Removed all `*u8` pointers and forward structs from `AstNode`, unified `WhenStmtArm` and `WhenArm`, and moved semantic annotations into `SemanticTable`. |
| **Phase 4** | `compiler.parser` | `fb128b8` | Renamed files to `src/compiler/parser/{parser,declaration,statement,expression,types}.kb` and eliminated ~500 lines of duplicated parsing logic. |
| **Phase 5** | `compiler.loader` | `b260a74` | Integrated `SourceMap` into `ModuleLoader` (`src/compiler/loader/loader.kb`), assigning a unique `file_id: u32` to every loaded source file and threading it through `Lexer` → `Token.span` → `Parser` → `AstNode.span` → `driver/diagnostics.kb`. |

---

## 3. Phase 6 (Current Priority): `src/compiler/semantic/` Ground-Up Rebuild

### 3.1 Why `src/compiler/sema/` Must Be Replaced

The legacy `src/compiler/sema/` directory (28 files, ~290 KB) violates our core engineering rules:
1. **Root `sema/` is a kitchen sink**: Mixes core types (`types.kb`, `prims.kb`, `structs.kb`, `enums.kb`, `methods.kb`, `traits.kb`), symbol tables (`symbol.kb`), generic templates/instances (`template.kb`, `instance.kb`), AST type synthesis (`type_ast.kb`), and the final lowering pass (`lower.kb`, which defines methods on `BodyPass`).
2. **Module path hack in `sema/mono/`**: Files in `src/compiler/sema/mono/{registry,instantiate,clone,rewrite}.kb` declare `mod compiler.sema.decl;` at the top to inject methods into `DeclPass`, violating the `module path == directory path` rule.
3. **Coupled pass state (`DeclPass` inside `BodyPass`)**: `BodyPass` embeds a full `pub decl: DeclPass` by value and copies `self.decl.symtab = self.symtab` back and forth during expression checking just to resolve AST types or instantiate generic functions.
4. **Desugaring attached to `std.memory.Arena`**: `sema/desugar/{loop,interp}.kb` defines AST desugaring transformations as methods on `impl Arena` instead of a dedicated semantic lowering component.
5. **Monolithic `body/call.kb`**: Nearly 1,000 lines (~40 KB) mixing function calls, method resolution, operator overloading, generic type unification/inference (`unify_type_pattern`, `infer_fn_type_args`), default arguments, and struct/enum constructors.

---

### 3.2 Target Subsystem Architecture (`src/compiler/semantic/`)

`src/compiler/semantic/` is partitioned into **6 strictly layered subsystems**. Each directory is a single Kobel module whose `mod` declaration matches its directory path 100%:

```
compiler.semantic.types
          ▲
          │
compiler.semantic.symbols
          ▲
          │
compiler.semantic.resolution
          ▲
          ├────────────────────────────┐
          │                            │
compiler.semantic.declaration   compiler.semantic.checker
                                       ▲
                                       │
                            compiler.semantic.lowering
```

#### Subsystem 1: `compiler.semantic.types` (`src/compiler/semantic/types/`)
*Responsibility*: Pure representation of Kobel types, primitive singletons, composite type metadata, memory layouts, and `Type` → `AstNode` synthesis. Zero knowledge of scopes, symbol tables, or compiler passes.
- **Dependencies**: `std.*`, `compiler.core`, `compiler.ast`
- **Files**:
  - `type.kb` *(from `sema/types.kb`)*: `DataType`, `Type`, `TypeInfo`, `PointerType`, `ArrayType`, `FunctionType`, `Type.equals`, `Type.mangled_name`, `Type.align`, `Type.size`.
  - `primitive.kb` *(from `sema/prims.kb`)*: `PrimTypes`, primitive singleton initialization, primitive size/alignment/classification helpers.
  - `composite.kb` *(from `sema/structs.kb`, `sema/enums.kb`, `sema/methods.kb`, `sema/traits.kb`)*:
    - `StructField`, `StructType`, struct layout computation (`layout_struct`).
    - `EnumMemberInfo`, `EnumInfo`, `EnumType`, tagged-union layout computation.
    - `MethodInfo`, `MethodParam`.
    - `TraitInfo`, `TraitMethod`, `TraitAssoc`.
  - `bridge.kb` *(from `sema/type_ast.kb`)*: `ast_type_from_type` (converts a resolved `*Type` back into a syntactic `*AstNode` for generic substitution/materialization).

#### Subsystem 2: `compiler.semantic.symbols` (`src/compiler/semantic/symbols/`)
*Responsibility*: Semantic database storing scopes, module imports, symbols, per-node side-table annotations (`SemanticTable`), and generic templates/instances (`GenericReg`).
- **Dependencies**: `std.*`, `compiler.core`, `compiler.ast`, `compiler.semantic.types`
- **Files**:
  - `symbol.kb` *(from `sema/symbol.kb`)*: `SymbolKind`, `Symbol`, `Scope`, `ModuleScope`, `ImportBinding`.
  - `table.kb` *(from `sema/symbol.kb`)*: `SymbolTable` (module registration, symbol lookup across module/import/global scopes, enum/trait registration).
  - `annotations.kb` *(from `sema/symbol.kb` & `sema/instance.kb`)*: `SemanticTable`, `IsPatternInfo`, `AnnEntry`, `InstanceAnnotations` (side-tables mapping `*AstNode` → `*Type`, `*MethodInfo`, resolved `*AstNode`, `*InstanceRecord`, `IsPatternInfo`).
  - `generics.kb` *(from `sema/template.kb` & `sema/instance.kb`)*: `TemplateKind`, `GenTemplate`, `GenSubst`, `InstanceKind`, `InstanceRecord`, `GenericReg`.

#### Subsystem 3: `compiler.semantic.resolution` (`src/compiler/semantic/resolution/`)
*Responsibility*: Standalone type resolver and generic monomorphization engine (`TypeResolver`). Replaces the old `sema/decl/type.kb` + `sema/mono/*` hack so both `declaration` and `checker` can resolve types and instantiate generic templates cleanly without copying `SymbolTable` or embedding `DeclPass` inside `BodyPass`.
- **Dependencies**: `std.*`, `compiler.core`, `compiler.lexer`, `compiler.ast`, `compiler.semantic.types`, `compiler.semantic.symbols`
- **Files**:
  - `resolver.kb` *(from `sema/decl/pass.kb` & `sema/decl/type.kb`)*:
    - `TypeResolver` struct holding a pointer/reference to shared state (`symtab: *SymbolTable`, `arena: *Arena`, `errors: *List<str>`, `current_module: str`, `subst_params: List<str>`, `subst_types: List<*Type>`, `active_traits: List<str>`).
    - `resolve_ast_type`, `check_type_bounds`, `satisfies_trait`, `lookup_subst`, `push_subst` / `pop_subst`.
    - Primitive method lookup (`find_primitive_method`) and struct/trait method lookup helpers needed during resolution.
  - `registry.kb` *(from `sema/mono/registry.kb`)*: Template registration and lookup (`find_struct_template`, `find_fn_template`, `find_method_template`, `register_instance`, `find_instance`).
  - `instantiate.kb` *(from `sema/mono/instantiate.kb`)*: `instantiate_struct_type`, `instantiate_fn`, `instantiate_method`, `drain_instances`, and struct/enum layout helpers.
  - `clone.kb` *(from `sema/mono/clone.kb`)*: AST cloning with type substitution (`subst_clone_node`, `materialize_body`).
  - `rewrite.kb` *(from `sema/mono/rewrite.kb`)*: Post-instantiation AST type rewriting (`rewrite_decl_types`, `rewrite_stmt_types`, `rewrite_expr_types`).

#### Subsystem 4: `compiler.semantic.declaration` (`src/compiler/semantic/declaration/`)
*Responsibility*: Stage 2 top-level declaration collection (`collect_program`). Registers modules, imports, traits, structs, enums, `impl` blocks, functions, and global constants into `SymbolTable` and drives initial monomorphization via `TypeResolver`.
- **Dependencies**: `std.*`, `compiler.core`, `compiler.ast`, `compiler.semantic.types`, `compiler.semantic.symbols`, `compiler.semantic.resolution`
- **Files**:
  - `collector.kb` *(from `sema/decl/collect.kb`)*: `DeclarationCollector` / `collect_program` multi-pass orchestration (Pass 0: templates & traits; Pass 1: structs, enums, impls, functions; Pass 2: drain monomorphization worklist).
  - `registration.kb` *(from `sema/decl/pass.kb`)*: `collect_struct`, `collect_enum`, `collect_trait`, `collect_impl`, `collect_fn`, `collect_const`, `collect_use`.

#### Subsystem 5: `compiler.semantic.checker` (`src/compiler/semantic/checker/`)
*Responsibility*: Stage 3 function and method body type checking, generic call type inference, smart casting, and pattern exhaustiveness verification.
- **Dependencies**: `std.*`, `compiler.core`, `compiler.lexer`, `compiler.ast`, `compiler.semantic.types`, `compiler.semantic.symbols`, `compiler.semantic.resolution`
- **Files**:
  - `checker.kb` *(from `sema/body/pass.kb`)*: `BodyChecker` (or `BodyPass` alias for compatibility during transition), local scope stack, active generic instance environment (`push_instance_env` / `pop_instance_env`), decision channel helpers (`annotate_type`, `set_resolved`, `set_method`, `set_ident_name`), and error reporting.
  - `program.kb` *(from `sema/body/program.kb`)*: `check_program`, `infer_arrow_returns`, `check_decl`, `flush_instances`.
  - `statement.kb` *(from `sema/body/stmt.kb`)*: Statement checking (`var`/`val`, `if`, `while`, `for`, `return`, `when`, `defer`, `break`, `continue`).
  - `expression.kb` *(from `sema/body/expr.kb`)*: Literals, identifiers, unary/binary operators, type casts (`as`), range expressions, array literals.
  - `access.kb` *(from `sema/body/access.kb`)*: Field access (`.`), safe navigation (`?.`), Elvis (`?:`), indexing (`[]`), assignment and lvalue mutability validation.
  - `call.kb` *(from `sema/body/call.kb`)*: Function calls, method calls, operator overloading resolution, default argument splicing, and struct/enum constructors.
  - `inference.kb` *(extracted from `sema/body/call.kb`)*: Generic type argument unification (`unify_type_pattern`) and call-site type argument inference (`infer_fn_type_args`, `infer_method_type_args`).
  - `pattern.kb` *(from `sema/body/pattern.kb`)*: `is` expression checking, smart-cast scope narrowing, `when` pattern matching and exhaustiveness checking.
  - `interpolation.kb` *(from `sema/body/interp.kb`)*: String interpolation part checking and `Stringer` protocol verification.

#### Subsystem 6: `compiler.semantic.lowering` (`src/compiler/semantic/lowering/`)
*Responsibility*: Stage 3b post-check AST desugaring and lowering into C99-ready AST nodes (`lower_program`). No longer attaches methods to `std.memory.Arena` or `BodyPass`.
- **Dependencies**: `std.*`, `compiler.core`, `compiler.lexer`, `compiler.ast`, `compiler.semantic.types`, `compiler.semantic.symbols`, `compiler.semantic.resolution`, `compiler.semantic.checker`
- **Files**:
  - `loop.kb` *(from `sema/desugar/loop.kb`)*: `lower_for_range` and `lower_for_iterable` (`for` loop desugaring into `while` blocks).
  - `string.kb` *(from `sema/desugar/interp.kb`)*: String interpolation and string concatenation/comparison AST builders.
  - `lowering.kb` *(from `sema/lower.kb`)*: `LoweringPass` / `lower_program`, `lower_decl`, `lower_stmt`, `lower_expr`, generic instance body materialization (`materialize_instances`).

---

### 3.3 Step-by-Step Execution Checklist for Phase 6

To execute Phase 6 safely while keeping the compiler verifiable at every step:

- [x] **Step 6.1**: Create `src/compiler/semantic/types/{type,primitive,composite,bridge}.kb` (`mod compiler.semantic.types;`).
- [x] **Step 6.2**: Create `src/compiler/semantic/symbols/{symbol,table,annotations,generics}.kb` (`mod compiler.semantic.symbols;`).
- [x] **Step 6.3**: Create `src/compiler/semantic/resolution/{resolver,registry,instantiate,clone,rewrite}.kb` (`mod compiler.semantic.resolution;`). Eliminate the `mod compiler.sema.decl;` hack.
- [x] **Step 6.4**: Create `src/compiler/semantic/declaration/{collector,registration}.kb` (`mod compiler.semantic.declaration;`).
- [x] **Step 6.5**: Create `src/compiler/semantic/checker/{checker,program,statement,expression,access,call,inference,pattern,interpolation}.kb` (`mod compiler.semantic.checker;`). Split `inference.kb` cleanly out of `call.kb`.
- [x] **Step 6.6**: Create `src/compiler/semantic/lowering/{loop,string,lowering}.kb` (`mod compiler.semantic.lowering;`). Move `desugar` methods off `impl Arena` into `compiler.semantic.lowering`.
- [x] **Step 6.7**: Update all consumers (`src/compiler/codegen/*`, `src/compiler/driver/*`, `src/lsp/*`, `tests/unit/test_sema_*.kb`) to import `compiler.semantic.*` and delete `src/compiler/sema/` completely.
- [x] **Step 6.8**: Build with `.\build\seed\kobel_seed.exe build`, run `powershell -ExecutionPolicy Bypass -File scripts/check.ps1 -Update`, regenerate `dist/bootstrap.c`, rebuild `build/seed/kobel_seed.exe`, and commit Phase 6.

---

## 4. Subsequent Phases (Phases 7–8)

### Phase 7: `src/compiler/codegen/` Cleanup (Completed)
- [x] Rename `c_codegen.kb`, `c_program.kb`, `c_stmt.kb`, `c_expr.kb`, `c_type.kb`, `c_runtime.kb` to clean, non-abbreviated names under `src/compiler/codegen/`:
  - `generator.kb`, `program.kb`, `statement.kb`, `expression.kb`, `types.kb`, `runtime.kb`
- [x] Ensure `codegen` is 100% read-only over `AstNode` and `SemanticTable`.

### Phase 8: `driver`, `config`, `formatter`, `lsp` & Documentation Sync
- Review and clean up `src/compiler/driver/`, `src/config/`, `src/fmt/`, and `src/lsp/`.
- Update `docs/architecture.md`, `docs/sema.md`, `docs/parameterised-ir.md`, and `AGENTS.md` to reflect the finalized module hierarchy.

---

## 5. Technical Guardrails & Known Kobel Compiler Quirks

Any agent working on this codebase MUST keep these language/compiler mechanics in mind:

1. **Folder-as-Package Resolution (`ModuleLoader`)**:
   - When `use a.b.c.*` is imported, `ModuleLoader` loads either `src/a/b/c.kb` OR all `src/a/b/c/*.kb` files in alphabetical order.
   - All `.kb` files directly inside `src/a/b/c/` share the module namespace `mod a.b.c;` and can reference each other's declarations without `use`.
   - Subdirectories `src/a/b/c/d/` are separate modules (`mod a.b.c.d;`) and MUST be imported explicitly via `use a.b.c.d.*;`.
2. **Default Struct Field Initializer Splicing (`copy_expr`)**:
   - In Kobel, default field values on structs (e.g. `pub bag: DiagnosticBag = DiagnosticBag()`) are spliced as AST expressions at every constructor call site.
   - If a struct is constructed in a module that does not import the types or generic instantiations used by that default expression, compilation or runtime initialization will fail.
   - **Rule**: Initialize complex or generic struct fields inside `pub init(...)` rather than in struct field default expressions.
3. **Nullable Pointer Smart-Casting**:
   - Inside `if (ptr != null)`, Kobel automatically smart-casts `ptr` from `*T?` to non-null `*T`.
   - Do **not** write `ptr!` inside an `if (ptr != null)` branch; the compiler will reject `!` on an already smart-cast non-null pointer.
4. **Indexing `List<T>` Parameters**:
   - Subscript indexing `items[i]` requires `items` to be a `List<T>` value (or `var items: List<T>`), not a reference `&List<T>`. Since `List<T>` is a small header containing a heap buffer pointer (`data: *T`), passing `var items: List<T>` by value still mutates the underlying elements in-place if needed.
5. **Bootstrap & Verification Gate**:
   - Always compile new changes using the seed compiler first:
     `.\build\seed\kobel_seed.exe build`
   - Run the full test/snapshot/fixpoint suite:
     `powershell -ExecutionPolicy Bypass -File scripts/check.ps1` (add `-Update` if C output changes intentionally, e.g., when module/struct C mangled names change from `compiler__sema__*` to `compiler__semantic__*`).
   - Whenever a phase passes, regenerate the bootstrap C seed and rebuild `kobel_seed.exe`:
     ```powershell
     .\build\kobel.exe src/main.kb -emit-c dist/bootstrap.c
     cmd /c scripts\build_seed.bat
     ```
