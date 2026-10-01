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
transforms are independent opt-ins. Exact structural vertical-gap policies are a
narrow explicit override of the boundaries they own, not general reflow authority.
`Layout = Canonical` selects group layout from syntax, width, child decisions and
an explicit break requirement, not old newlines, previous passes or whether
another option changed the construct. With `IfNeeded`, a fitting eligible
canonical list/expression can flatten; `VerticalWhenBroken` controls packing,
not whether a fitting group must break.

Canonical topology decisions are independent of source newlines and horizontal
spelling discarded by rebuilding topology. Intentionally preserved,
non-topology-changing horizontal properties can still affect rendered width.
The horizontal `Preserve` contract below resolves that distinction before fitting.

These category keys accept `Inherit`, `Preserve` or `Canonical`:

- `CallsLayout`, `ParametersLayout`, `InitializersLayout`, `CapturesLayout`;
- `ControlHeadersLayout`, `BaseListsLayout`, `ConstructorListsLayout`;
- `EnumeratorsLayout`;
- `ExpressionsLayout`, `ChainsLayout`, `DeclarationHeadersLayout`;
- `BodiesLayout`, `LambdaBodiesLayout`.

All inherit global `Layout`, except `LambdaBodiesLayout`, which inherits resolved
`BodiesLayout`. Inheritance is compiled once, not looked up per token/group.
Declaration braces, including enum opening braces, use declaration-header
ownership; control/case braces use control-header ownership. Lambda opening
braces use declaration-header ownership, while lambda body topology uses
`LambdaBodiesLayout` independently. Enumerator contents have their own list
ownership, independent of `BodiesLayout`.

Preserved layout is distinct from **byte protection**. Directives, disabled
regions and whitespace-sensitive macro invocations must remain byte-exact under
both layout modes, including explicit transforms/spacing. Directives are never
reindented. `HonorClangFormatDirectives = true` honors clang-format off/on markers;
hit-fmt off/on markers always protect regions. `WhitespaceSensitiveMacros` retains
semantic protection hints and `CallLikeMacroNames` retains syntax hints. Neither
is an aesthetic name whitelist.

## Shared lists, break requirements and continuation anchors

| Global key | Values / default |
| --- | --- |
| `ListBreak` | `IfNeeded` (default), `Always` |
| `ListPacking` | `Fill` (default), `VerticalWhenBroken` |
| `ListFirstItem` | `InlineIfFits` (default), `NextLine` |
| `ListCloser` | `SameLine` (default), `OwnLine` |
| `ListAlignment` | `Block` (default), `Delimiter` |
| `ContinuationOffset` | 0 (use IndentWidth), or 1..32 |
| `AlignmentFallbackOffset` | 0 (use IndentWidth), or 1..32 |

For each list category (`Calls`, `Parameters`, `Initializers`, `Captures`,
`ControlHeaders`, `BaseLists`, `ConstructorLists`, `Enumerators`), prefix `Break`,
`Packing`, `FirstItem`, `Alignment`, `ContinuationOffset`, or
`AlignmentFallbackOffset` with that category name. Delimiter-bearing categories
(`Calls`, `Parameters`, `Initializers`, `Captures`, `ControlHeaders`, `Enumerators`)
additionally accept `Closer`. `<Family>Break` accepts
`Inherit | IfNeeded | Always`; the other overrides use the corresponding global
values plus `Inherit`. All default to `Inherit`, resolving to the corresponding
global key. Explicit zero offsets resolve to `IndentWidth`, not to the global
offset. `EnumeratorsLayout` inherits global `Layout`, as do the other list layout
categories.

`IfNeeded` permits fitting canonical groups to flatten. `Always` excludes flat
layout when a nonempty group has a legal editable boundary owned by that group.
It does not activate `Preserve` owners or alter protected bytes. It requires an
available owned break even when a child is already multiline: a child's break
cannot substitute for that requirement. Child breaks still have their normal
effect on actual rendered brokenness.

A break requirement is not a fabricated `Broken` flag. Empty groups, indivisible
expressions and groups without legal editable owned boundaries cannot be made
broken merely by setting `Always`. Safety and protection still win.

Packing remains independent of the requirement. `Fill` packs complete items into
the selected broken layout; `VerticalWhenBroken` gives each applicable owned item
boundary a break. Child groups and comments may require breaks inside an item.
First/closer placement applies **only to an actually broken group**.
`InlineIfFits` permits the first item beside the opener when it and required suffix
fit; it is a preference, not a requirement. `NextLine` puts it below the opener.
For a fitting singleton `f(x)`, `Always` can select the opener-to-item boundary
even with `InlineIfFits` and `SameLine`; `f()` cannot invent an item boundary.
The annotated first-string pin is also soft: `Always` outranks it when necessary
to satisfy an eligible owned boundary, including a singleton pinned string.
`OwnLine` puts a broken closer on the enclosing block column; `SameLine` keeps it
with the last item unless a hard boundary (e.g. a line comment) forbids that.
Base/constructor lists have no closing delimiter:
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
| `Preserve` | Retain source horizontal spacing on preserved/non-topology-rebuilt flat-only boundaries; use syntax-safe flat fallback before fitting at canonical topology-changing boundaries. |
| `Space` | Prefer one horizontal space on a flat gap. |
| `NoSpace` | Prefer no horizontal space on a flat gap. |
| `GeneratedSpace` | Space when the owning canonical combinator rebuilds the property, or for an inserted token adjacency. |
| `GeneratedNoSpace` | NoSpace when the owning canonical combinator rebuilds the property, or for an inserted token adjacency. |

Horizontal `Preserve` retains available source spelling on preserved boundaries
and genuinely flat-only adjacencies that do not rebuild topology. At canonical
topology-changing boundaries, it instead uses the syntax-safe flat fallback
**before fitting**, even when the selected boundary ultimately stays flat.
This prevents fitting from depending on horizontal spelling a break would discard.
It applies generally, not only to tabs, and requires no text-history comparison,
retry or cleanup pass. The same canonical boundary uses the same fallback on
every invocation; there is no weaker idempotence exception.

Lowering determines topology ownership structurally, not from the final decision
to break, token provenance or whether surrounding text changed. This distinction
complements `GapContext`; it is not another public spacing value.

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
`ControlBracePlacement` replaces `ControlBlockBracePlacement`.
`EnumBracePlacement` uses the same values and defaults to `Attached`. Brace
placement owns the header-to-opening-brace boundary; list closer policy does not
place body braces. `NextLineIfHeaderBroken` observes its own selected header
group's break decision, not an unrelated group or the following body's shape.

### Independent body shapes and namespace indentation

| Key | Values / default and inheritance |
| --- | --- |
| `BodyShape` | `Expanded` (default), `CompactIfFits`; nonempty bodies only |
| `EmptyBodyShape` | `Expanded` (default), `CompactIfFits`; independent of `BodyShape` |
| `LambdaBodyShape` | `Inherit` (default) → `BodyShape`, `Expanded`, `CompactIfFits` |
| `LambdaEmptyBodyShape` | `Inherit` (default) → `EmptyBodyShape`, `Expanded`, `CompactIfFits` |
| `IndentNamespaces` | `Preserve` (default), `Indent`, `NoIndent` |
| `NamespaceIndentWidth` | 0 (default, use `IndentWidth`), or 1..32; used only by `Indent` |

Body shapes replace collapse/expansion history flags. They cover ordinary compound
blocks, braced control/case bodies, function bodies, lambda bodies, record bodies
and namespace bodies. They apply under `BodiesLayout`, except lambdas use
`LambdaBodiesLayout`. Shape selection applies only to canonical bodies;
preserved bodies retain topology. Enums and initializer/capture/argument lists use
list semantics, not body shape. Requirement bodies (`requires` expressions) are
outside this extension.

An empty body has no interior tokens or comments after authorized structural
transforms. A comment-only body is nonempty. Null statements are not empty unless
an enabled safe transform removes them. `CompactIfFits` puts the interior and
closing brace on one line when width, required owned boundaries and safety permit;
it does not move the opening brace governed by brace placement. Preserved
topology and protection take precedence over body shape. `BodyShape = Expanded` with
`EmptyBodyShape = CompactIfFits` can still compact an eligible empty lambda when
`LambdaBodyShape = Expanded` and `LambdaEmptyBodyShape = Inherit`.

Namespace indentation reuses `Preserve | Indent | NoIndent`. `Preserve` retains
the existing namespace indentation relationship. `NoIndent` places contents at
the namespace's enclosing rendered block base; `Indent` adds resolved
`NamespaceIndentWidth`. Zero width means `IndentWidth`, not zero indentation,
and never inherits `BlockBodyIndentWidth`. A nested namespace starts from its
parent's content base. `namespace A::B { ... }` has one body, hence one indentation
application. Explicit modes use rendered structural anchors, not the opening
brace's source column, and do not change ordinary function/control indentation.

Block, member, access-label, case-label and statement-attribute indentation remain
orthogonal, with their existing explicit offsets. Record member and namespace
indentation are independent of body shape. Comments follow syntax attachment,
not an independent indentation pass.

`InitializersTrailingComma = Preserve | Never | WhenMultiline` is an explicit
structural action at grammar-approved sites only. `WhenMultiline` removes the
comma in flat output and adds it in multiline output. `Preserve` is the default.
There is deliberately no always-add mode. Reserve optional-comma width during
layout, then choose punctuation from the resulting shape. **Source commas never
select line shape or closer placement.** This removes the old
`SameLineUnlessTrailingComma` cycle.

### Enum header and enumerator ownership

`EnumBracePlacement` is owned by resolved `DeclarationHeadersLayout` and governs
the enum header-to-opening-brace boundary. `NextLineIfHeaderBroken` observes that
enum header group, never whether enumerators break.

Contents use the shared delimiter-bearing `Enumerators*` family. Public keys are
`EnumeratorsLayout`, `EnumeratorsBreak`, `EnumeratorsPacking`,
`EnumeratorsFirstItem`, `EnumeratorsCloser`, `EnumeratorsAlignment`,
`EnumeratorsContinuationOffset` and `EnumeratorsAlignmentFallbackOffset`.
All default to `Inherit`: layout resolves to global `Layout`, and the remaining
keys resolve to the corresponding shared list defaults/offsets above. Explicit
zero offsets resolve to `IndentWidth`. This covers scoped and unscoped enums,
including attributes, prefixes and underlying type clauses recognized by syntax.
Enumerator initializer expressions remain child expression groups. Comments retain attachment and hard-boundary behavior.
`BodiesLayout = Preserve` does not disable explicit canonical enumerator layout.

A comment-free empty enum has no enumerator boundary to force. Canonical `{}`
remains flat even with `EnumeratorsBreak = Always`; preserved topology and hard
boundaries still win. Neither `BodyShape` nor `EmptyBodyShape` governs enums.
Existing enum trailing commas are preserved and included in fit accounting.
They do not select brokenness or closer placement. `InitializersTrailingComma`
does not add or remove enum commas; no new enum comma transform is introduced.

## Expressions, chains and width diagnostics

| Key | Values / default |
| --- | --- |
| `ExpressionsBreak` | `IfNeeded` (default), `Always`; independent of `ListBreak` |
| `ExpressionsPacking` | `Fill` (default), `VerticalWhenBroken` |
| `ChainsBreak` | `IfNeeded` (default), `Always`; independent of `ListBreak` |

Expressions reuse the shared packing type. Lowering builds precedence- and
associativity-aware owned operator sequences; each segment retains its operand
subtree as a child group. Outer policies do not flatten nested operands into one
token sequence. In `a + b * c + d`, the outer `+` sequence retains `b * c` as a
child, whose multiplication group can independently apply expression policy.
`ExpressionsBreak` selects whether an eligible fitting sequence must break;
`ExpressionsPacking` controls placement afterward. `Fill` packs owned segments;
`VerticalWhenBroken` breaks each applicable owned operator boundary.

`BinaryOperatorSide`, `AssignmentOperatorSide`, `ShiftOperatorSide` and
`TernaryOperatorSide` all accept `Leading | Trailing`. Shift is a syntax category,
not an operator spelling list. Defaults are trailing binary/assignment/shift and
leading ternary. These side policies remain orthogonal to break and packing.
`TernaryLayout = Flexible | ThreePart`; `ThreePart` is a stronger local shape
constraint: once that ternary group breaks, both question and colon boundaries
break, including under `Fill`. It does not alone force a fitting ternary to break;
`ExpressionsBreak = Always` can require an eligible owned break. The boundaries
belong to the ternary group, independently of brace placement and initializer
trailing commas. There is no source-newline activation mode.

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

## Structural vertical gaps

These policies own identified syntax boundaries and count **empty lines**, not
newline characters. At a broken boundary, `n` empty lines require `n + 1` newline
sequences. Counts and caps are validated in 0..10000 before narrowing or computing
newline counts.

| Key | Values / default and inheritance |
| --- | --- |
| `NamespaceBlankLinesAfterOpen` | `Preserve` (default), exact count 0..10000 |
| `NamespaceBlankLinesBeforeClose` | `Preserve` (default), exact count 0..10000 |
| `AccessSectionBlankLinesBefore` | `Preserve` (default), exact count 0..10000 |
| `AccessSectionBlankLinesAfterLabel` | `Preserve` (default), exact count 0..10000 |
| `DefinitionBlankLines` | `Preserve` (default), exact count 0..10000 |
| `MemberDefinitionBlankLines` | `Inherit` (default) → `DefinitionBlankLines`, `Preserve`, exact count 0..10000 |
| `MaxEmptyLines` | `Preserve` (default), upper bound 0..10000 |

An explicit count is exact, subject to safety/protection, not a minimum. Zero
removes surplus empty lines without itself changing the boundary's selected
inline-versus-broken status. A positive count requires an applicable boundary to
break. `Preserve` adds no exact-count or break requirement.

Explicit vertical policies own their gap independently of surrounding `Preserve`
topology. This is a narrow override, not authority to reflow neighboring
declarations, bodies or expressions. Thus a positive namespace count can own an
applicable gap inside a preserved namespace body, while `CallsLayout = Preserve`
still ignores `CallsBreak = Always`.

| Boundary policy | Exact applicability |
| --- | --- |
| Namespace after-open / before-close | Edges of nonempty namespace content, outside attached first/last content comments. A genuinely empty body has neither item boundary. |
| Access before | Before a subsequent explicit access section in a record, not before the first section at body opening. |
| Access after-label | Between an access label, including any same-line trailing comment, and following section content; no phantom boundary for an empty section. |
| Definition separation | Between neighboring complete declaration items in file, namespace or record declaration sequences when at least one item is a function, record or enum definition. |
| Member definition separation | The same definition rule inside record sequences; resolves instead of adding to `DefinitionBlankLines`. |

Definition separation is not inserted between two ordinary variables or two
forward declarations. Lambda expressions, access labels and namespace definitions
are not themselves qualifying function/record/enum definition items. Declaration
attributes, template prefixes and leading documentation comments stay with their
declaration; trailing comments stay with the preceding item. The boundary follows
a complete item, including any required terminating semicolon. Separation before
`/// documentation` and `[[nodiscard]] int Read() { ... }` belongs before the
documentation, not between it and the declaration.

Directive/protection boundaries are barriers; declaration adjacency is not
inferred through them. Unattached comments remain content/barriers rather than
being moved to manufacture a gap. Namespace comment-only bodies are nonempty
content, not compact empty bodies. Positive namespace edge counts do not expand
`namespace N {}`: it has no first/last-content boundary. Empty bodies and empty
access sections never acquire phantom content edges from vertical policies.

`MaxEmptyLines` caps excess existing empty lines in editable vertical gaps. It
does not force an inline boundary to break or remove a required line-comment
newline, even at zero. Protected bytes and token-interior newlines are outside
its authority.

An explicit cap rejects any explicit/resolved exact count above it, including
inherited member counts. Validate the final snapshot independently of current
input or surrounding layout mode. `DefinitionBlankLines = 2` with
`MaxEmptyLines = 1` is invalid even under `Layout = Preserve`. Report both
conflicting options, with configuration locations where available; never silently
clamp, choose specificity or repair output later.

Lowering selects one owned vertical gap. The generic renderer emits its newline
sequences and indentation only after the final newline. If token-owned CR requires
LF-first completion, it affects only the first newline; remaining sequences use
the global newline encoding. Protection and mandatory lexical boundaries always
outrank editable gap policies.

## Three library-aware layout annotations

Exactly three library-aware layout features remain. The familiar name lists are
annotations resolved against parsed syntax before layout. They select generic
policies; the renderer receives typed groups and annotations, never identifier
checks. There are no ad-hoc text rewrite passes or user-rule DSL.

1. **Fluent chains** retain `ChainedCallRootNames` (default `[]`) as root-selection
   annotations for the shared `ChainsLayout`, `ChainsBreak`, `ChainsPacking` and
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
   when syntax, hard boundaries, the global width and owned break requirements
   permit; `Always` outranks this soft pin when needed to satisfy an available
   owned boundary. Remaining arguments use the shared call-list policy. Nonstring
   first arguments use the normal list policy. The annotation does not change
   string bytes or create a width override.

Annotations respect preserved topology and byte protection. Semantic macro safety
(`WhitespaceSensitiveMacros`) and parser hints (`CallLikeMacroNames`) remain
separately supported; they are not additional aesthetic layout features.

## Configuration examples

All examples describe proposed capabilities, not current implementation support
or exact upstream presets.

A canonical block-anchored style with independently preserved lambda topology:

```text
Layout = Canonical
IndentWidth = 4
ColumnLimit = 80
ListBreak = IfNeeded
ListPacking = VerticalWhenBroken
ListAlignment = Block
ContinuationOffset = 4
CallsFirstItem = NextLine
CallsCloser = SameLine
BodyShape = Expanded
EmptyBodyShape = Expanded
LambdaBodiesLayout = Preserve
ControlBracePlacement = NextLineIfHeaderBroken
TernaryLayout = ThreePart
InitializersTrailingComma = WhenMultiline
CommaSpaceBefore = GeneratedNoSpace
CommaSpaceAfter = GeneratedSpace
```

A fitting `f(a, b)` stays flat under `VerticalWhenBroken + IfNeeded`; choosing
`CallsBreak = Always` instead requires an eligible owned break. Packing alone is
not force.

A synthesized YTsaurus-inspired policy independently selects the six capabilities:

```text
Layout = Canonical
IndentWidth = 4
ColumnLimit = 120
ListBreak = IfNeeded
ListAlignment = Block
ListPacking = VerticalWhenBroken
ListFirstItem = NextLine
CallsBreak = Always
ChainsBreak = Always
ChainsPacking = VerticalWhenBroken
ExpressionsBreak = Always
ExpressionsPacking = VerticalWhenBroken
BodyShape = Expanded
EmptyBodyShape = CompactIfFits
LambdaBodyShape = Expanded
LambdaEmptyBodyShape = Inherit
EmptyBlockInteriorSpacing = NoSpace
IndentNamespaces = NoIndent
IndentMembers = Indent
IndentAccessModifiers = NoIndent
FunctionBracePlacement = NextLine
ClassBracePlacement = NextLine
EnumBracePlacement = NextLine
EnumeratorsBreak = Always
EnumeratorsCloser = OwnLine
NamespaceBlankLinesAfterOpen = 1
NamespaceBlankLinesBeforeClose = 1
AccessSectionBlankLinesBefore = 1
AccessSectionBlankLinesAfterLabel = 1
DefinitionBlankLines = 1
MemberDefinitionBlankLines = 0
MaxEmptyLines = 1
```

Illustrative structure, assuming the enum comma already exists in source:

```cpp
namespace Sample {

enum class State
{
    Idle,
    Active,
};

class Worker
{
public:

    void Clear();

private:

    int Value;
};

void Idle()
{}

int Process()
{
    Apply(
        left,
        right);
    flow
        .Step()
        .Finish();
    return first +
        second +
        third;
}

}
```

Identifiers have no special behavior. Empty function compactness controls its
interior, independently of `FunctionBracePlacement = NextLine`. Namespace
`NoIndent` does not remove the four-column ordinary function-body indent.

A delimiter-aligned LLVM/Google-like policy uses the same global width and
fallback contract without requiring fitting groups to break:

```text
Layout = Canonical
ColumnLimit = 80
ListBreak = IfNeeded
ChainsBreak = IfNeeded
ExpressionsBreak = IfNeeded
ListPacking = Fill
ExpressionsPacking = Fill
ListAlignment = Delimiter
ContinuationOffset = 4
AlignmentFallbackOffset = 4
IndentNamespaces = NoIndent
BaseListsLayout = Preserve
ConstructorListsLayout = Preserve
```

Enum brace placement, empty-body shape and exact structural gaps can be chosen
independently without changing alignment or introducing another width budget.
A canonical fill-list separator with `CommaSpaceAfter = Preserve` uses the same
syntax-safe flat fallback before every fit, even if source spacing contains a tab
or that boundary ultimately remains flat.

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
| `Enforce*` | Canonical category layout for shape; explicit `Break = Always` for required owned boundaries; typed source/generated gap values for spacing. |
| `MergeFunctionParameters`, `MergeInitializerList` | Canonical eligible groups can flatten when they fit under `IfNeeded`; `Always` requires an available owned break; Preserve retains topology. |
| `FunctionParameter*`, first function parameter | `Parameters*`; Pack -> Fill, OnePerLine -> VerticalWhenBroken; NameLine/NameLineIfFits -> InlineIfFits. |
| `CallArgument*`, first/sole argument, call closing parenthesis | `Calls*`; every arity follows the shared rule, with annotated string-first pinning where applicable. |
| `InitializerList*`, first initializer element | `Initializers*`; separate closer from trailing comma. |
| `InheritanceList*`, `ConstructorInitializerList*` | `BaseLists*`, `ConstructorLists*`; Format -> Canonical. |
| `*Indent = Continuation` | `*Alignment = Delimiter`, with explicit fallback offset. |
| `*Indent = IndentWidth` | `*Alignment = Block`, with continuation offset. |
| `ControlHeaderClauseSplit`, `ControlConditionIndent`, `ForHeaderClauseIndent`, multiline control positions | Shared `ControlHeaders*`; no separate for-clause indentation exception. |
| `ExpressionLayoutCondition` Overflow/FitOrOverflow/Always | `ExpressionsLayout = Canonical`; Overflow/FitOrOverflow → `ExpressionsBreak = IfNeeded`, Always → `ExpressionsBreak = Always`; no source-history activation modes. |
| Expression packing | `ExpressionsPacking = Fill` or `VerticalWhenBroken`, independently of break requirement and operator side. |
| `TernarySplit` AsNeeded/ThreeLines | `TernaryLayout` Flexible/ThreePart. |
| Leading/trailing operator spelling lists | Shared operator-side categories, including `ShiftOperatorSide`; arbitrary spelling overrides are not supported. |
| Chained-call split/layout/indent | `ChainsLayout/Break/Packing/Alignment`; Preserve retains topology, canonical IfNeeded permits fitting groups to flatten, old Always requires `ChainsBreak = Always`; shared groups cover member and repeated invocation. |
| `ChainedCallRootNames`, `CallLineFirstStringNames`, `IndentedCallScopePairs` | Retained as annotations for the three generic library-aware policies. |
| `CallArgumentSplitColumnLimit`, `CallArgumentSplitColumnLimitNames` | Remove; use the single global `ColumnLimit`, without named width overrides. |
| `BaseListsCloser`, `ConstructorListsCloser` | Remove; delimiterless lists have no closer. Following body braces use brace-placement policies. |
| `IndentBlockBodies`, nonempty/closing-brace body flags | `BodiesLayout`, nonempty `BodyShape`; indentation remains independent. |
| Empty-body collapse/expansion flags, including `CollapseEmptyFunctionBodies` | Independent `EmptyBodyShape`; defaults to Expanded, not inherited from `BodyShape`; enums/lists/requirement bodies excluded. |
| `SingleStatementLambdaBodyLayout` | `LambdaBodiesLayout`, nonempty `LambdaBodyShape`; use Preserve explicitly to retain source topology. |
| Empty lambda body shape | `LambdaEmptyBodyShape` inherits `EmptyBodyShape`, never `LambdaBodyShape`. |
| Namespace indentation | `IndentNamespaces` and `NamespaceIndentWidth`; explicit NoIndent is zero relative indent, width zero resolves to `IndentWidth`. |
| Enum header/content layout | `EnumBracePlacement` under declaration-header ownership; independent shared `Enumerators*` lists, with existing enum commas preserved. |
| Namespace/access/definition vertical spacing | Exact named empty-line counts; member count inherits definition count; `MaxEmptyLines` is an independently validated cap, never silent clamping. |
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

Proposed profiles explicitly opt into Canonical. They are approximate styles, not
byte-for-byte upstream presets: fitting eligible source multiline groups can
flatten under `IfNeeded`, while `Always` requires a legal owned break. Safe nested
lists/chains share policies, control clauses share one anchor policy, empty and
nonempty body shapes are independent, and commas do not drive closer placement.
The three library-aware annotations remain supported; arbitrary call-specific
width or rewrite exceptions do not.
LLVM/Google context-dependent base/constructor lists use explicit Preserve
overrides. Migration is a configuration change, not a claim of identical output.

## Proposed compilation and library contract

The proposed scalar policy layer is parser/engine-independent. Its library
contract separates compact scalar policies from owning annotation metadata:

- `CompiledPolicies CompilePolicies(const Style&)` validates dimensions, resolves
  inheritance/zero offsets and exact-count/cap conflicts, then returns an
  immutable, trivially copyable scalar snapshot. Compile after final overrides
  and reuse across files, not per group. Counts/caps are checked in 0..10000
  before narrowing or newline arithmetic, independently of input/layout mode.
- Compile an owning annotation snapshot for the three library-aware name/pair
  lists after the same final overrides. It must not retain views into mutable raw
  configuration; syntax analysis uses it to attach typed generic annotations.
  Mutating raw style requires new scalar and annotation snapshots.
- `void ValidatePolicyStyle(const Style&)` validates raw programmatic dimensions
  and resolved exact-count/cap consistency; invalid widths, offsets, counts or
  conflicts report `std::invalid_argument`. Config errors include source path
  and line; cap conflicts identify both options and their locations when available.
- `constexpr std::optional<bool> GapUsesSpace(GapPolicy, GapContext) noexcept`:
  true means Space, false means NoSpace, disengaged means preserve/fallback.
  `GapContext` is `Preserved | Rebuilt`, supplied explicitly by the property's
  owning lowering combinator, independently of token provenance. Canonically
  regenerated gaps and inserted token adjacencies use Rebuilt; unrelated nested
  edits never activate preserved owners. This is an ownership contract, not an
  insertion-only rule. A disengaged result is not unconditional source copying:
  lowering's structural topology distinction selects the syntax-safe flat fallback
  before fit for canonical topology-changing gaps, including selected-flat gaps.
- `std::string_view ConfigMigrationHint(std::string_view)` returns advice only;
  it never accepts an old option.

Scalar policies cover layout, width diagnostics, indentation kind and resolved
indentation controls, trailing whitespace, pointer/reference declarator location,
list/header/expression/chain/body/brace policies, initializer trailing commas,
syntax spacing and compact typed vertical-gap exact counts/cap. List policies,
including `Enumerators`, carry resolved break requirement and packing; expressions
carry independent break requirement and shared packing; chains carry their
independent break requirement. Body policies resolve nonempty and empty shapes
separately, including each lambda inheritance path. Enum opening braces use
header ownership; enum contents remain lists with preserved commas.

Delimiter-bearing lists have a closer policy; delimiterless header lists have no
public closer control. Indentation policy carries alignment, continuation and
fallback offsets. Access/member/case/attribute zero offsets resolve to
`IndentWidth`; body indentation uses its resolved block-body width. Namespace
indentation has its own resolved mode/width, with zero width resolving to
`IndentWidth`, not block-body width. Vertical policies resolve member inheritance
and reject exact counts above an explicit cap once after final overrides. Retained
scalar layout, spacing, indentation, declarator, vertical-gap and text-output
controls do not require mutable raw style during lowering.

`LoadConfigFile(path)` continues to return raw `Configuration`. Naming/parameter
diagnostics and exclusions remain outside the layout snapshots. Semantic syntax
and protection hints remain separate metadata. Explicit structural actions may
read the final raw style, but must obey grammar and protection constraints.
Compile scalar policies and the owning annotation snapshot after final CLI
overrides, then reuse them across files. There is **no mutable Configuration
cache of compiled snapshots**, so configuration mutation cannot silently leave
stale policies in use.
