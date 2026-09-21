# Configuration

`hit-fmt` searches for a `.hit-fmt` file beside each source file and then in
its parent directories. The nearest file wins. Pass `--config <file>` to use
one file for every input, or `--no-config` to use built-in defaults.

The format is a small `Key = Value` language. Blank lines and `#` comments are
allowed.

```text
IndentWidth = 4
ColumnLimit = 120
Regroup = false
InsertBraces = true
BreakInlineRecordDeclarations = true
SpaceBeforeTemplateParameterPack = false
FunctionBracePlacement = "NewLine"
RecordBracePlacement = "NewLine"
ControlBracePlacement = "Preserve"
LambdaBracePlacement = "Preserve"
NamespaceBracePlacement = "Attach"
SpaceBeforeParentheses = "ControlStatements"
PointerAlignment = "Preserve"
ReferenceAlignment = "Preserve"
IndentCaseLabels = "Preserve"
SpaceInsideParentheses = false
SpaceInsideSquareBrackets = false
IndentNamespaces = false
KeepEmptyBlocksOnOneLine = true
AllowShortLambdasOnOneLine = true
OpenBracketAlignment = "Continuation"
CloseBracketAlignment = "AlignWithOpening"
ForbidBreakAfter = ["Format", "LOG_DEBUG"]
WhitespaceSensitiveMacros = ["DEFINE_ENUM", "ALIGNED_VALUES"]
```

## Settings

- `IndentWidth`: positive integer used for generated indentation.
- `ColumnLimit`: positive integer soft limit for generated wrapping.
- `Regroup`: when `true`, collapses a safe multiline call or list if its flat
  representation fits within `ColumnLimit`. Constructs containing comments,
  blank lines, nested multiline constructs, directives, or macro calls are not
  regrouped.
- `InsertBraces`: adds braces to supported unbraced `if` and `else` bodies.
- `BreakInlineRecordDeclarations`: places anonymous `struct` and `union`
  braces and their following declarators on separate lines. Set it to `false`
  to retain `union { ... } value;` layout.
- `SpaceBeforeTemplateParameterPack`: inserts a space before the ellipsis in a
  template type parameter pack, as in `typename ...Args`. It does not change
  pack expansions such as `Args...` or fold-expression spacing.
- `FunctionBracePlacement`, `RecordBracePlacement`,
  `ControlBracePlacement`, `LambdaBracePlacement`, and
  `NamespaceBracePlacement`: accept `Preserve`, `Attach`, or `NewLine`.
- `SpaceBeforeParentheses`: accepts `Never`, `ControlStatements`, or `Always`.
- `PointerAlignment` and `ReferenceAlignment`: independently control `*` and
  `&`/`&&` declarators and accept `Preserve`, `Left`, `Right`, or `Middle`.
- `IndentCaseLabels`: accepts `Preserve`, `Outdent`, or `Indent`.
- `SpaceInsideParentheses`: inserts spaces inside non-empty parentheses.
- `SpaceInsideSquareBrackets`: inserts spaces inside ordinary subscript
  brackets, without changing lambdas, attributes, or structured bindings.
- `IndentNamespaces`: indents namespace contents.
- `KeepEmptyBlocksOnOneLine`: keeps supported empty bodies as `{ }`.
- `AllowShortLambdasOnOneLine`: permits compact one-statement lambdas when
  their source form is also compact.
- `OpenBracketAlignment`:
  - `Preserve`: retain existing item indentation.
  - `Continuation`: use one continuation indentation level.
  - `AlignWithOpening`: align items with the position after `(` or `[`.
- `CloseBracketAlignment`:
  - `Preserve`: retain the source indentation of an existing closing bracket.
  - `AlignWithOpening`: align it with the line containing the opening bracket.
- `ForbidBreakAfter`: replaces the default list of functions and macros whose
  first string argument remains beside the opening parenthesis.
- `WhitespaceSensitiveMacros`: replaces the default list of macro invocations
  whose contents are emitted verbatim.

Array settings replace, rather than extend, their built-in defaults. Use an
empty array to disable a list.
