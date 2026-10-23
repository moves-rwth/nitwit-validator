# Module `ast`

The abstract syntax tree shared by parser, semantic analysis, interpreter and validation.

- `type.hpp`: types and `QualType`.
- `decl.hpp`: declarations, `RecordLayout`, `TranslationUnit`.
- `expr.hpp`, `stmt.hpp`: expressions and statements.
- `context.hpp`: `AstContext`, the arena owning all nodes, plus type creation and canonicalization.
- `casting.hpp`: `isa`, `cast`, `dyn_cast`.
- `dump.hpp`: text dumps for golden tests.

## Life cycle of a tree

1. **The parser** creates the nodes. It binds every name to its declaration, and resolves typedef names, tags and
   labels. Types are built as written: typedef sugar, array sizes as expressions, `typeof`.
2. **Semantic analysis** completes the same nodes in place:
   - the types and value categories of expressions;
   - `ImplicitCastExpr`s;
   - resolved array sizes and `typeof`;
   - fields of member expressions;
   - semantic initializer lists;
   - enumeration values, bit-field widths, record layouts, case values;
   - the merged types of redeclarations;
   - the `locals` of functions.
3. **The interpreter and the validation layer** only read the tree.

All nodes are trivially destructible. They live in the `AstContext` arena and refer to each other with raw pointers
and spans.

## Conventions

- Every node has a `SourceRange`. Nodes that witnesses refer to keep the extra locations the formats use: the `?` of
  a conditional expression, the `)` of a call, the `while` of a `do` loop.
- Sema mutates a tree only through setters, and only to complete it; it never removes or reorders statements.
