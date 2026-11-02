# Semantic Analysis (Sema) Map

This document details the semantic analysis architecture of the Kobel compiler (`src/compiler/sema`): data ownership, decision channels, and the lifecycle of monomorphized instances.

---

## 1. Pipeline Stages

Semantic analysis executes in two primary pipeline steps coordinated by `src/compiler/driver/pipeline.kb`:

```
                 load_program (Stage 1)
                           │
                           ▼
               collect_program (Stage 2)
        ┌──────────────────┴──────────────────┐
        │  Pass 0: Register templates & traits │
        │  Pass 1: Collect structs & methods  │
        │  Pass 2: Instantiate generic types   │
        │  Pass 3: Drain monomorph worklist    │
        └──────────────────┬──────────────────┘
                           │
                           ▼
             check_program & lower (Stage 3)
        ┌──────────────────┴──────────────────┐
        │  infer_arrow_returns (Fixpoint)     │
        │  check_program (Side-table typing)  │
        │  flush_instances (Generic methods)  │
        │  lower_program (Desugar for C99)    │
        └──────────────────┬──────────────────┘
                           │
                           ▼
                  gen_program (Stage 4)
```

---

## 2. Module Directory Layout

```
src/compiler/sema/
├── body/                  # Function & method body checking
│   ├── pass.kb            # Core BodyPass visitor & scope stack
│   ├── expr.kb            # Expression typing & operators
│   ├── call.kb            # Function, method, and constructor calls
│   ├── access.kb          # Member dot-access, array indexing, and assignments
│   ├── pattern.kb         # 'when' and 'is' pattern matching & smart casting
│   ├── stmt.kb            # Statement checking (while, return, defer)
│   └── program.kb         # Body checking driver & arrow return inference
├── decl/                  # Declaration collection & type resolution
│   ├── pass.kb            # Core DeclPass (structs, methods, enums, traits)
│   ├── collect.kb         # Top-level declaration collection driver
│   └── type.kb            # AST type node resolution (resolve_ast_type)
├── mono/                  # Monomorphization engine
│   ├── registry.kb        # Template registration & instance worklist
│   ├── instantiate.kb     # Struct/function view instantiation
│   ├── clone.kb           # AST cloner for instance materialization
│   └── rewrite.kb         # Type substitution & generic call rewriting
├── desugar/               # Syntactic sugar lowering helpers
│   ├── loop.kb            # 'for' loop translation
│   └── interp.kb          # String interpolation translation
├── types.kb               # DataType, Type, TypePayload, and pointer/array payloads
├── methods.kb             # MethodInfo and PrimitiveMethod payloads
├── structs.kb             # StructField, StructType, and memory layout calculator
├── enums.kb               # EnumInfo, EnumMemberInfo, and payload enum layouts
├── prims.kb               # Canonical PrimTypes singletons (i8..i64, u8..u64, bool, etc.)
├── symbol.kb              # Symbol, Scope, ModuleScope, and SymbolTable
├── instance.kb            # InstanceRecord, InstanceKind, and AnnEntry
├── traits.kb              # TraitInfo and TraitMethod contracts
├── template.kb            # GenTemplate, GenSubst, and TemplateKind
└── lower.kb               # Final AST rewriting pass for C emission
```

---

## 3. Data Ownership Model

| Data Structure | Defined In | Written By | Read By |
|---|---|---|---|
| `AstNode` | `compiler.ast.*` | Parser (Stage 1) | Sema, Codegen (Read-only) |
| `Type` | `sema.types` | `DeclPass` via `Arena` | Sema, Codegen |
| `PrimTypes` | `sema.prims` | `SymbolTable` (Singleton) | All Sema passes |
| `StructType` / `StructField` | `sema.structs` | `DeclPass` (`collect_struct`, `layout_struct`) | Sema, Codegen |
| `EnumInfo` | `sema.enums` | `DeclPass` (`collect_enum`) | Sema, Codegen |
| `MethodInfo` | `sema.methods` | `DeclPass` (`collect_impl`) | BodyPass, Lower |
| `SymbolTable` | `sema.symbol` | `DeclPass` (registers symbols) | BodyPass (lookups) |
| `GenTemplate` | `sema.template` | `decl.collect` (Pass 0) | `mono.registry` |
| `InstanceRecord` | `sema.instance` | `mono.instantiate` | BodyPass, Lower |
| `AnnEntry` | `sema.instance` | `BodyPass` (writes to `InstanceRecord.ann_map`) | `mono.clone` (materialization) |

---

## 4. The Four Decision Channels

To ensure that generic AST templates remain immutable, `BodyPass` never mutates AST nodes directly. Instead, decisions flow strictly through four standard channels:

```
                      BodyPass Decision Channels
                                   │
     ┌──────────────────┬──────────┴──────────┬──────────────────┐
     ▼                  ▼                     ▼                  ▼
annotate_type()    set_resolved()        set_method()       set_ident_name()
 (type_ptr)       (resolved_node)     (resolved_method)    (Mangled C name)
```

| Channel | Recorded Value | Purpose | Consumer |
|---|---|---|---|
| `annotate_type(node, ty)` | `node.type_ptr` or `ann.type_ptr` | Resolves the concrete data type of an expression. | Lower, Codegen |
| `set_resolved(node, target)` | `node.resolved_node` or `ann.resolved_node` | Replaces complex syntax (e.g. enum constants, `T.size()`, desugared `for` loop). | Lower, Codegen |
| `set_method(node, mi)` | `node.resolved_method` or `ann.resolved_method` | Records the resolved method overload or operator binding. | Lower |
| `set_ident_name(node, name)` | `ann.ident_name` | Records mangled C identifiers for generic instances. | Lower (`materialize_body`) |

---

## 5. Lifecycle of a Generic Instance

1. **Registration:** Generic structs and functions are recorded as `GenTemplate` descriptors during Pass 0 of declaration collection.
2. **Instantiation Request:** When a concrete type reference (e.g. `List<i32>`) or call (e.g. `arena.alloc<Point>()`) is encountered:
   - Type arguments are resolved to concrete `*Type` pointers.
   - The instance cache is queried using `(template, concrete_types)`.
   - If not yet present, a new `InstanceRecord` is created and queued into `symtab.gen.instances`.
3. **Monomorphization Fixpoint:** `drain_instances()` iteratively processes queued instances until all transitively referenced generic types are instantiated.
4. **Body Checking:** `BodyPass` checks the instance body using the template AST while binding type parameters in `push_instance_env()`. All type annotations and method resolutions are recorded in the instance's side table (`ann_map`).
5. **Flush:** Generic methods encountered during body checking are queued and flushed in a secondary pass (`flush_instances`).
6. **Materialization & Lowering:** In `lower_program`, `materialize_body()` clones the template AST once for each concrete instance, applies the side-table annotations from `ann_map`, and lowers high-level constructs into C-compatible AST nodes.
7. **Code Generation:** Codegen walks the materialized declarations and emits C99 functions and typedefs.
