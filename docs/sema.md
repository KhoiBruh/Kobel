# Semantic Analysis (`compiler.semantic`) Map

This document details the semantic analysis architecture of the Kobel compiler (`src/compiler/semantic`): data ownership, decision channels, and the lifecycle of monomorphized instances.

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
        │  Pass 0.5: Forward-declare structs   │
        │  Pass 1: Collect enums & structs     │
        │  Pass 2: Rewrite non-templates       │
        │  Pass 3: Drain monomorph worklist    │
        │  Pass 4: Append instances to AST     │
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
src/compiler/semantic/
├── types/                 # Type system & memory layouts
│   ├── type.kb            # DataType, Type, TypePayload, and pointer/array payloads
│   ├── primitive.kb       # Canonical PrimTypes singletons
│   ├── composite.kb       # StructField, StructType, EnumInfo, layouts
│   └── bridge.kb          # Type <-> AST node conversions
├── symbols/               # Symbol tables, generics, and side tables
│   ├── symbol.kb          # Symbol, Scope, ModuleScope
│   ├── table.kb           # SymbolTable (primitives, modules, lookups)
│   ├── generics.kb        # Generic templates, instances, and registry
│   └── annotations.kb     # Side-table annotations and pattern metadata
├── resolution/            # Type resolution and monomorphization
│   ├── resolver.kb        # Standalone AST type resolver
│   ├── registry.kb        # Template registry and worklist
│   ├── instantiate.kb     # Generic template instantiation
│   ├── clone.kb           # AST cloning and body materialization
│   ├── subst.kb           # Generic substitution
│   └── rewrite.kb         # Generic AST rewrite pass
├── declaration/           # Declaration collection
│   ├── collector.kb       # Multi-pass declaration collector driver
│   └── registration.kb    # Struct, enum, method, and trait registration
├── checker/               # Semantic body checker
│   ├── checker.kb         # Core BodyChecker visitor and scope stack
│   ├── program.kb         # Driver for modules, functions, impls
│   ├── statement.kb       # Statement analysis (blocks, loops, returns, defer)
│   ├── expression.kb      # Expression typing and operators
│   ├── access.kb          # Member dot-access, indexing, and assignments
│   ├── call.kb            # Function, method, and constructor calls
│   ├── inference.kb       # Generic argument deduction and infer_type
│   ├── pattern.kb         # When expressions, is patterns, smart casts
│   └── interpolation.kb   # String interpolation and Stringer checking
└── lowering/              # Syntactic sugar lowering
    ├── lowering.kb        # AST lowering visitor and driver
    ├── loop.kb            # 'for' loop translation
    └── string.kb          # String interpolation desugaring
```

---

## 3. Data Ownership Model

| Data Structure | Defined In | Written By | Read By |
|---|---|---|---|
| `AstNode` | `compiler.ast.*` | Parser (Stage 1) | Semantic, Codegen (Read-only) |
| `Type` | `compiler.semantic.types` | `DeclarationCollector` via `Arena` | Semantic, Codegen |
| `PrimTypes` | `compiler.semantic.types` | `SymbolTable` (Singleton) | All Semantic passes |
| `StructType` / `StructField` | `compiler.semantic.types` | `DeclarationCollector` | Semantic, Codegen |
| `EnumInfo` | `compiler.semantic.types` | `DeclarationCollector` | Semantic, Codegen |
| `MethodInfo` | `compiler.semantic.types` | `DeclarationCollector` | BodyChecker, Lowering |
| `SymbolTable` | `compiler.semantic.symbols` | `DeclarationCollector` | BodyChecker (lookups) |
| `GenericTemplate` | `compiler.semantic.symbols` | `DeclarationCollector` (Pass 0) | Resolution registry |
| `InstanceRecord` | `compiler.semantic.symbols` | `Resolution.instantiate` | BodyChecker, Lowering |
| `IsPatternInfo` | `compiler.semantic.symbols` | `BodyChecker` (in `pattern.kb`) | Codegen |

---

## 4. The Four Decision Channels

To ensure that generic AST templates remain immutable, `BodyChecker` never mutates AST nodes directly. Instead, decisions flow strictly through four standard channels:

```
                     BodyChecker Decision Channels
                                   │
     ┌──────────────────┬──────────┴──────────┬──────────────────┐
     ▼                  ▼                     ▼                  ▼
annotate_type()    set_resolved()        set_method()       set_ident_name()
 (type_ptr)       (resolved_node)     (resolved_method)    (Mangled C name)
```

| Channel | Recorded Value | Purpose | Consumer |
|---|---|---|---|
| `annotate_type(node, ty)` | `node.type_ptr` or `ann.type_ptr` | Resolves the concrete data type of an expression. | Lowering, Codegen |
| `set_resolved(node, target)` | `node.resolved_node` or `ann.resolved_node` | Replaces complex syntax (e.g. enum constants, `T.size()`, desugared `for` loop). | Lowering, Codegen |
| `set_method(node, mi)` | `node.resolved_method` or `ann.resolved_method` | Records the resolved method overload or operator binding. | Lowering |
| `set_ident_name(node, name)` | `ann.ident_name` | Records mangled C identifiers for generic instances. | Lowering (`materialize_body`) |

---

## 5. Lifecycle of a Generic Instance

1. **Registration:** Generic structs and functions are recorded as `GenericTemplate` descriptors during Pass 0 of declaration collection.
2. **Instantiation Request:** When a concrete type reference (e.g. `List<i32>`) or call (e.g. `arena.alloc<Point>()`) is encountered:
   - Type arguments are resolved to concrete `*Type` pointers.
   - The instance cache is queried using `(template, concrete_types)`.
   - If not yet present, a new `InstanceRecord` is created and queued into `symtab.gen.instances`.
3. **Monomorphization Fixpoint:** `drain_instances()` iteratively processes queued instances until all transitively referenced generic types are instantiated.
4. **Body Checking:** `BodyChecker` checks the instance body using the template AST while binding type parameters in `push_instance_env()`. All type annotations and method resolutions are recorded in the instance's side table (`ann_map`).
5. **Flush:** Generic methods encountered during body checking are queued and flushed in a secondary pass (`flush_instances`).
6. **Materialization & Lowering:** In `lower_program`, `materialize_body()` clones the template AST once for each concrete instance, applies the side-table annotations from `ann_map`, and lowers high-level constructs into C-compatible AST nodes.
7. **Code Generation:** Codegen walks the materialized declarations and emits C99 functions and typedefs.
