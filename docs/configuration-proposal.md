# Group-policy configuration proposal (breaking change)

> **Proposal, not current implementation documentation.** This document defines
> the intended configuration and layout contract for review. It does not claim
> that the formatter already implements or validates these behaviors.

There are no compatibility aliases and no legacy layout fallback in the proposal.
The flat `Key = Value` syntax, `#` comments, quoted arrays, naming checks and `Exclude`
regexes remain. Unknown/removed keys fail with a path, line and migration advice.
`--help-config` explains the policy families; `--generate-config` enumerates all
supported keys, values and defaults.

## Layout ownership

`Layout = Preserve` is the safe default. It retains source **break topology**,
not necessarily every byte: explicit spacing, trimming, indentation and structural
transforms are independent opt-ins. `Layout = Canonical` selects group layout
from syntax, width and child decisions, not old newlines, previous passes or
whether another option changed the construct. A fitting canonical list/expression
can flatten; `VerticalWhenBroken` never forces a fitting group to break.

These category keys accept `Inherit`, `Preserve` or `Canonical`:

- `CallsLayout`, `ParametersLayout`, `InitializersLayout`, `CapturesLayout`;
- `ControlHeadersLayout`, `BaseListsLayout`, `ConstructorListsLayout`;
- `ExpressionsLayout`, `ChainsLayout`, `DeclarationHeadersLayout`;
- `BodiesLayout`, `LambdaBodiesLayout`.

All inherit global `Layout`, except `LambdaBodiesLayout`, which inherits resolved
`BodiesLayout`. Inheritance is compiled once, not looked up per token/group.
Declaration braces use declaration-header ownership; control/case braces use
control-header ownership. Lambda opening braces use declaration-header ownership,
while lambda body topology uses `LambdaBodiesLayout` independently.

Preserved layout is distinct from **byte protection**. Directives, disabled
regions and whitespace-sensitive macro invocations must remain byte-exact under
both layout modes, including explicit transforms/spacing. Directives are never
reindented. `HonorClangFormatDirectives = true` honors clang-format off/on markers;
hit-fmt off/on markers always protect regions. `WhitespaceSensitiveMacros` retains
semantic protection hints and `CallLikeMacroNames` retains syntax hints. Neither
is an aesthetic name whitelist.

## Shared lists and continuation anchors

| Global key | Values / default |
| --- | --- |
| `ListPacking` | `Fill` (default), `VerticalWhenBroken` |
| `ListFirstItem` | `InlineIfFits` (default), `NextLine` |
| `ListCloser` | `SameLine` (default), `OwnLine` |
| `ListAlignment` | `Block` (default), `Delimiter` |
| `ContinuationOffset` | 0 (use IndentWidth), or 1..32 |
| `AlignmentFallbackOffset` | 0 (use IndentWidth), or 1..32 |

For each list category (`Calls`, `Parameters`, `Initializers`, `Captures`,
`ControlHeaders`, `BaseLists`, `ConstructorLists`), prefix `Packing`, `FirstItem`,
`Alignment`, `ContinuationOffset`, or `AlignmentFallbackOffset` with that category
name. Delimiter-bearing categories (`Calls`, `Parameters`, `Initializers`,
`Captures`, `ControlHeaders`) additionally accept `Closer`. These overrides accept
`Inherit` (the default). Explicit zero offsets resolve to `IndentWidth`, not to
the global offset.

First/closer placement applies **only to a broken group**. `Fill` packs complete
items; `VerticalWhenBroken` gives each item a boundary break. Child groups and
comments may require additional breaks inside an item. `InlineIfFits` permits the
first item beside the opener only when it and required suffix can fit;
`NextLine` puts it below the opener. `OwnLine` puts a broken closer on the enclosing
block column; `SameLine` keeps it with the last item unless a hard boundary (e.g.
a line comment) forbids that. Base/constructor lists have no closing delimiter:
`BaseListsCloser` and `ConstructorListsCloser` are not public keys and are rejected
with migration advice. Use `ClassBracePlacement` or `FunctionBracePlacement` for
the following body brace, independently of list packing.

`Block` anchors continuation at block column + offset. `Delimiter` aligns after
the opener, with block + offset as the minimum. When alignment leaves no room for
the next indivisible segment and required suffix, fall back to block + fallback
offset. Alignment uses rendered columns, not copied source columns. LLVM and
Google explicitly use delimiter alignment with a 4-column continuation/fallback;
repository/YTsaurus examples use block alignment. Expressions use block
continuation; `ChainsAlignment = Delimiter` anchors to the rendered chain root.

This is the intended LLVM/Google alignment contract, not a claim of integrated
engine support. Checking only whether the anchor reaches `ColumnLimit` is
insufficient: the next indivisible segment **plus required suffix** must fit even
when the anchor itself is below the limit.

`BaseListsColonSide`, `BaseListsCommaSide`, `ConstructorListsColonSide` and
`ConstructorListsCommaSide` accept `Leading` (default) or `Trailing`. They decide
which side of a selected boundary owns punctuation, not whether a list breaks.
Colon gaps have category-prefixed `ColonSpaceBefore/After` keys; list commas use
shared `CommaSpaceBefore/After` policies.

## Gaps, braces, bodies and commas

Every syntax spacing field in the proposal has the same `GapPolicy` values:

| Value | Meaning |
| --- | --- |
| `Preserve` | Retain available source horizontal spacing; use syntax-safe defaults where no flat spelling exists. |
| `Space` | Prefer one horizontal space on a flat gap. |
| `NoSpace` | Prefer no horizontal space on a flat gap. |
| `GeneratedSpace` | Space when the owning canonical combinator rebuilds the property, or for an inserted token adjacency. |
| `GeneratedNoSpace` | NoSpace when the owning canonical combinator rebuilds the property, or for an inserted token adjacency. |

Break decisions are owned by layout policies, not gap-spacing values. Generated
layout ownership and source-token identity/provenance are separate axes. A
canonical call-list rebuild uses generated edge/separator spacing even where
the same token adjacency existed in source. Generated policies are **not limited
to token insertions**. A nested argument edit does **not** activate generated
policies on a preserved outer call's gaps. Inserted gaps always qualify as rebuilt.
Lowering supplies explicit `GapContext = Preserved | Rebuilt` for each owned
property; it must never infer this from before/after text or arbitrary changes to
an enclosing construct. Lexical separation, line comments, hard boundaries and
protection always win.

There are no `Enforce*` booleans. Before/after values replace compound colon, return
arrow, overloaded operator and comma spacing enums. For example:

```text
CommaSpaceBefore = NoSpace
CommaSpaceAfter = Space
ReturnArrowSpaceBefore = Space
ReturnArrowSpaceAfter = Space
EmptyBlockInteriorSpacing = GeneratedNoSpace
```

All brace-placement options use `Attached`, `NextLine` or
`NextLineIfHeaderBroken`. The last value observes the selected header group
break decision, never a before/after-text snapshot. Existing keys for class,
struct, union, function, lambda, namespace and case block remain;
`ControlBracePlacement` replaces `ControlBlockBracePlacement`. Brace placement
owns the header-to-brace boundary; list closer policy does not place body braces.

`BodyShape = Expanded | CompactIfFits` replaces body collapse/expansion history
flags. `LambdaBodyShape = Inherit | Expanded | CompactIfFits` inherits `BodyShape`.
Shape only controls canonical bodies; preserved bodies retain topology. Block,
member, access-label, case-label and statement-attribute indentation remain
orthogonal, with their existing explicit offsets. Comments follow syntax
attachment, not an independent indentation pass.

`InitializersTrailingComma = Preserve | Never | WhenMultiline` is an explicit
structural action at grammar-approved sites only. `WhenMultiline` removes the
comma in flat output and adds it in multiline output. `Preserve` is the default.
There is deliberately no always-add mode. Reserve optional-comma width during
layout, then choose punctuation from the resulting shape. **Source commas never
select line shape or closer placement.** This removes the old
`SameLineUnlessTrailingComma` cycle.

## Expressions, chains and width diagnostics

`BinaryOperatorSide`, `AssignmentOperatorSide`, `ShiftOperatorSide` and
`TernaryOperatorSide` all accept `Leading | Trailing`. Shift is a syntax category,
not an operator spelling list. Defaults are trailing binary/assignment/shift and
leading ternary. `TernaryLayout = Flexible | ThreePart`; `ThreePart` couples both
ternary boundaries when broken, but leaves a fitting flat ternary flat. The
question/colon boundaries belong to the ternary group, independently of brace
placement and initializer trailing commas.

`ChainsPacking = Fill | VerticalWhenBroken` and `ChainsAlignment = Block | Delimiter`
apply to safely parsed fluent chains through `.`, `->` and repeated invocation
such as `factory()(arg)(next)`. Call arguments remain child list groups rather
than being flattened into the chain. The annotations below select generic chain
policies, not identifier-specific renderer behavior.

`WidthDiagnostics = Off | Report` replaces `LineLengthCheck`. Report compares
emitted line widths to `ColumnLimit`, including unavoidable/protected overflows.
There is no Soft mode and no extra format-to-test-feasibility pass. Defaults remain
`IndentWidth = 4`, `ColumnLimit = 120`, `WidthDiagnostics = Off`; width ranges are
1..32 for indentation and 1..10000 for columns. There is exactly one global
`ColumnLimit`: `CallArgumentSplitColumnLimit` and
`CallArgumentSplitColumnLimitNames` are removed without a replacement named-width
scheme. Library annotations never introduce a second width budget.

## Three library-aware layout annotations

Exactly three library-aware layout features remain. The familiar name lists are
annotations resolved against parsed syntax before layout. They select generic
policies; the renderer receives typed groups and annotations, never identifier
checks. There are no ad-hoc text rewrite passes or user-rule DSL.

1. **Fluent chains** retain `ChainedCallRootNames` (default `[]`) as root-selection
   annotations for the shared `ChainsLayout`, `ChainsPacking` and
   `ChainsAlignment` policies. Recognition covers `.`, `->` and repeated
   invocation, including mixed chains; it is not limited to member access.
   Ordinary safely parsed chains use the same group machinery. Annotation
   matching uses parsed root identity, not substring matching of source text.
2. **Matched fluent/visual call scopes** retain `IndentedCallScopePairs` (default
   `[]`) as quoted `"Open:Close"` pairs selecting a generic statement-scope
   indentation policy. Pairing uses parsed statement sequences, receiver identity
   and call form; wrapper calls must have equivalent wrapper context except for
   the paired method. Nesting must be validated before a scope is annotated.
   Pairing never crosses a protected region or preprocessor boundary. Uncertain,
   mismatched or unmatched sequences remain source-owned. Existing visual
   indentation preservation is retained: an already visually nested, matched
   scope preserves its extra interior level when surrounding indentation is
   normalized; flat nested pairs retain the parent's interior level. Merely
   listing a pair does not force all matching calls into new visual indentation.
3. **First-line message calls** retain `CallLineFirstStringNames` (default `[]`)
   as annotations selecting a generic first-argument pin. The pin applies only
   when the first argument is an appropriate complete string-literal expression,
   including parsed adjacent literal concatenation, not an expression merely
   beginning with a literal. It keeps that argument on the call's first line
   when syntax, hard boundaries and the global width permit; remaining arguments
   use the shared call-list policy. Nonstring first arguments use the normal list
   policy. The annotation does not change string bytes or create a width override.

Annotations respect preserved topology and byte protection. Semantic macro safety
(`WhitespaceSensitiveMacros`) and parser hints (`CallLikeMacroNames`) remain
separately supported; they are not additional aesthetic layout features.

## Configuration examples

A canonical block-anchored style with an independently preserved lambda body:

```text
Layout = Canonical
IndentWidth = 4
ColumnLimit = 80
ListPacking = VerticalWhenBroken
ListAlignment = Block
ContinuationOffset = 4
CallsFirstItem = NextLine
CallsCloser = SameLine
BodyShape = Expanded
LambdaBodiesLayout = Preserve
ControlBracePlacement = NextLineIfHeaderBroken
TernaryLayout = ThreePart
InitializersTrailingComma = WhenMultiline
CommaSpaceBefore = GeneratedNoSpace
CommaSpaceAfter = GeneratedSpace
```

A delimiter-aligned LLVM/Google-like policy uses the same global width and
fallback contract; it is not an exact upstream preset:

```text
Layout = Canonical
ColumnLimit = 80
ListAlignment = Delimiter
ContinuationOffset = 4
AlignmentFallbackOffset = 4
BaseListsLayout = Preserve
ConstructorListsLayout = Preserve
```

Library annotations are independent of width and semantic macro hints:

```text
ChainedCallRootNames = ["MakeFlow"]
ChainsLayout = Canonical
ChainsPacking = VerticalWhenBroken
CallLineFirstStringNames = ["WriteMessage"]
IndentedCallScopePairs = ["BeginItems:EndItems"]
CallLikeMacroNames = ["CHECK_RESULT"]
WhitespaceSensitiveMacros = ["VERBATIM_TABLE"]
Exclude = ["(^|/)generated(/|$)"]
```

## Migration map and deliberate differences

| Old option family | Replacement |
| --- | --- |
| `Enforce*` | Canonical category layout for shape; typed source/generated gap values for spacing. |
| `MergeFunctionParameters`, `MergeInitializerList` | Canonical groups flatten when they fit; Preserve retains topology. |
| `FunctionParameter*`, first function parameter | `Parameters*`; Pack -> Fill, OnePerLine -> VerticalWhenBroken; NameLine/NameLineIfFits -> InlineIfFits. |
| `CallArgument*`, first/sole argument, call closing parenthesis | `Calls*`; every arity follows the shared rule, with annotated string-first pinning where applicable. |
| `InitializerList*`, first initializer element | `Initializers*`; separate closer from trailing comma. |
| `InheritanceList*`, `ConstructorInitializerList*` | `BaseLists*`, `ConstructorLists*`; Format -> Canonical. |
| `*Indent = Continuation` | `*Alignment = Delimiter`, with explicit fallback offset. |
| `*Indent = IndentWidth` | `*Alignment = Block`, with continuation offset. |
| `ControlHeaderClauseSplit`, `ControlConditionIndent`, `ForHeaderClauseIndent`, multiline control positions | Shared `ControlHeaders*`; no separate for-clause indentation exception. |
| `ExpressionLayoutCondition` Overflow/FitOrOverflow/Always | `ExpressionsLayout = Canonical`; no source-history activation modes. |
| `TernarySplit` AsNeeded/ThreeLines | `TernaryLayout` Flexible/ThreePart. |
| Leading/trailing operator spelling lists | Shared operator-side categories, including `ShiftOperatorSide`; arbitrary spelling overrides are not supported. |
| Chained-call split/layout/indent | `ChainsLayout/Packing/Alignment`; shared groups cover member and repeated-invocation chains. |
| `ChainedCallRootNames`, `CallLineFirstStringNames`, `IndentedCallScopePairs` | Retained as annotations for the three generic library-aware policies. |
| `CallArgumentSplitColumnLimit`, `CallArgumentSplitColumnLimitNames` | Remove; use the single global `ColumnLimit`, without named width overrides. |
| `BaseListsCloser`, `ConstructorListsCloser` | Remove; delimiterless lists have no closer. Following body braces use brace-placement policies. |
| `IndentBlockBodies`, nonempty/collapse/closing-brace body flags | `BodiesLayout`, `BodyShape`; generic expanded or compact-if-fits bodies. |
| `SingleStatementLambdaBodyLayout` | `LambdaBodiesLayout`, `LambdaBodyShape`; use Preserve explicitly to retain source topology. |
| `CommaSpacing`, colon/arrow/overload compound spacing | Before/After GapPolicy keys. |
| `LineLengthCheck` Strict/Soft | `WidthDiagnostics = Report`; unavoidable overflow is reported too. |

**Explicitly unsupported:** name-specific call limits; `SeparatorLength`
(rewriting comment spelling); `PreprocessorIndentation` (rewriting protected
directives). Protect a table or other byte-sensitive region with off/on markers
when source layout must remain exact. Do not replace semantic whitespace-sensitive
macro protection with layout preservation.

Safe explicit structural settings remain: include ordering, namespace comments,
missing control braces, null statements/declarations and empty control bodies.
Their syntax/safety checks remain mandatory; the config schema is not permission
to transform unknown or protected syntax. Naming/prefix/parameter diagnostics and
file exclusions remain independent.

Profiles explicitly opt into Canonical. They are approximate styles, not
byte-for-byte upstream presets: fitting source multiline constructs can flatten,
safe nested lists/chains share policies, control clauses share one anchor policy,
and commas do not drive closer placement. The three library-aware annotations
remain supported; arbitrary call-specific width or rewrite exceptions do not.
LLVM/Google context-dependent base/constructor lists use explicit Preserve
overrides. Migration is a configuration change, not a claim of identical output.

## Proposed compilation and library contract

The proposed scalar policy layer is parser/engine-independent. Its library
contract separates compact scalar policies from owning annotation metadata:

- `CompiledPolicies CompilePolicies(const Style&)` validates dimensions, resolves
  inheritance/zero offsets and returns an immutable, trivially copyable scalar
  snapshot. Compile after final overrides and reuse across files, not per group.
- Compile an owning annotation snapshot for the three library-aware name/pair
  lists after the same final overrides. It must not retain views into mutable raw
  configuration; syntax analysis uses it to attach typed generic annotations.
  Mutating raw style requires new scalar and annotation snapshots.
- `void ValidatePolicyStyle(const Style&)` validates raw programmatic dimensions;
  validation reports invalid widths/offsets with `std::invalid_argument`. Config
  errors include source path and line.
- `constexpr std::optional<bool> GapUsesSpace(GapPolicy, GapContext) noexcept`:
  true means Space, false means NoSpace, disengaged means preserve/fallback.
  `GapContext` is `Preserved | Rebuilt`, supplied explicitly by the property's
  owning lowering combinator, independently of token provenance. Canonically
  regenerated gaps and inserted token adjacencies use Rebuilt; unrelated nested
  edits never activate preserved owners. This is an ownership contract, not an
  insertion-only rule.
- `std::string_view ConfigMigrationHint(std::string_view)` returns advice only;
  it never accepts an old option.

Scalar policies cover layout, width diagnostics, indentation kind and resolved
indentation controls, trailing whitespace, pointer/reference declarator location,
list/header/expression/chain/body/brace policies, initializer trailing commas and
syntax spacing. Delimiter-bearing lists have a closer policy; delimiterless
header lists have no public closer control. Indentation policy carries alignment,
continuation and fallback offsets. Access/member/case/attribute zero offsets resolve to
`IndentWidth`; body indentation uses its resolved block-body width. Retained
scalar layout, spacing, indentation, declarator and text-output controls do not
require mutable raw style during lowering.

`LoadConfigFile(path)` continues to return raw `Configuration`. Naming/parameter
diagnostics and exclusions remain outside the layout snapshots. Semantic syntax
and protection hints remain separate metadata. Explicit structural actions may
read the final raw style, but must obey grammar and protection constraints.
Compile scalar policies and the owning annotation snapshot after final CLI
overrides, then reuse them across files. There is **no mutable Configuration
cache of compiled snapshots**, so configuration mutation cannot silently leave
stale policies in use.
