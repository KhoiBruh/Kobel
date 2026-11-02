# Parameterised Instance Model & Monomorphization

This document explains how the Kobel compiler implements generics monomorphization using parameterised instance records and side tables, without cloning AST trees during semantic analysis.

---

## 1. Architectural Goal

Traditional monomorphization clones the entire AST of a generic template every time a new type argument is encountered, leading to AST memory bloat and duplicate typechecking.

Kobel avoids this by decoupling **semantic typechecking** from **code emission**:
- The template AST is parsed once and remains immutable.
- Concrete instances are typechecked against the shared template AST using substitution environments.
- Per-instance semantic decisions are stored in a side table.
- AST nodes are duplicated only once during lowering to materialize the final C99 code.

---

## 2. Core Components

```
                    GenTemplate (AST Template)
                                │
          ┌─────────────────────┴─────────────────────┐
          ▼                                           ▼
List<i32> InstanceRecord                    List<str> InstanceRecord
├── Concrete Types: [*Type (i32)]          ├── Concrete Types: [*Type (str)]
├── Mangled C Name: "std__List_i32"        ├── Mangled C Name: "std__List_str"
└── ann_map (Side Table)                   └── ann_map (Side Table)
    ├── node_1 -> AnnEntry (type: i32*)         ├── node_1 -> AnnEntry (type: str*)
    └── node_2 -> AnnEntry (ident: ...)         └── node_2 -> AnnEntry (ident: ...)
```

### 1. `GenTemplate` (`src/compiler/sema/template.kb`)
Stores the original generic declaration (`struct`, `fn`, or `impl`), its type parameter names (`T`, `K`, `V`), and the owning module.

### 2. `InstanceRecord` (`src/compiler/sema/instance.kb`)
Represents a specialized instance of a template.
- **Identity:** Uniquely keyed by `(template, args: List<*Type>)`. Lookup uses structural type equivalence (`Type.equals`), preventing redundant instantiations across modules.
- **Mangled Name:** Deterministically generated from the template's module and the mangled C names of its type arguments (e.g. `std__List_i32`).

### 3. View-Based Struct Instantiation (`src/compiler/sema/mono/instantiate.kb`)
When a generic struct is instantiated:
- Type parameters are bound to concrete `*Type` values in `DeclPass.subst_types`.
- Field types are resolved against this environment to construct a concrete `StructType` and compute exact memory layouts (`layout_struct`).
- The template struct AST is **never cloned**; a lightweight declaration view is constructed solely for the C code generator.

### 4. Per-Node Side Table (`InstanceRecord.ann_map`)
When `BodyPass` typechecks a function or method body for an instance:
- `push_instance_env(rec)` activates the concrete type parameter bindings.
- Expressions in the shared template AST are visited.
- Type annotations, resolved method calls, and mangled identifier names are written to `rec.ann_map` (`HashMap<usz, *AnnEntry>`) indexed by node address.
- `pop_instance_env()` restores the outer environment.

### 5. Emission Materialization (`materialize_body`)
In `src/compiler/sema/mono/clone.kb`:
- During Stage 3 (`lower_program`), `materialize_body(rec)` clones the template AST into a concrete AST tree.
- The recorded decisions from `rec.ann_map` are applied onto the newly created nodes.
- High-level syntax is lowered, and the resulting declarations are appended to the compilation unit for C99 emission.

---

## 3. Worklist and Dependency Order

1. **Initial Monomorphization:** In Stage 2 (`collect_program`), all explicit generic uses in declarations are collected and enqueued into `symtab.gen.instances`.
2. **Worklist Drain (`drain_instances`):** Instantiating one generic type may introduce transitive references to others (e.g., `Map<K, V>` referencing `List<Entry<K, V>>`). The worklist repeatedly rewrites and instantiates until reaching a fixpoint.
3. **Creation-Order Append:** Instances are appended to the program in creation order. The C code generator relies on forward typedefs and natural dependency sorting to handle struct order.
4. **Late Method Flushing (`flush_instances`):** Generic method calls (e.g. `arena.alloc<T>()`) can only be fully resolved during Stage 3 (`check_program`) when the receiver type is known. Any instances generated during body checking are queued and checked in a late flush pass before lowering.
