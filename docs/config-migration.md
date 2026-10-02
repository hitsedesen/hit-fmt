# Group-policy configuration (breaking change)

There are no compatibility aliases and no legacy layout fallback. The flat
`Key = Value` syntax, `#` comments, quoted arrays, naming checks and `Exclude`
regexes remain. Unknown/removed keys fail with a path, line and migration advice.
`--help-config` explains the policy families; `--generate-config` enumerates all
supported keys, values and defaults.

## Layout ownership

`Layout = Preserve` is the safe default. It retains source **break topology**,
not necessarily every byte: explicit spacing, trimming, indentation and structural
transforms are independent opt-ins. `Layout = Canonical` selects group layout
from syntax, width and child decisions, not old newlines, previous passes or
whether another option changed the construct. A fitting canonical list/expression
can flatten under `IfNeeded`; `VerticalWhenBroken` alone never forces a fitting
group to break. `Always` requires a legal editable boundary owned by a nonempty
canonical group, even when a child is already multiline. Empty/indivisible groups
cannot fabricate brokenness. Preserve and protection win; source newline history
never satisfies a required owned break. The soft first-message pin yields to
`Always` when no other eligible boundary can satisfy it.

These category keys accept `Inherit`, `Preserve` or `Canonical`:

- `CallsLayout`, `ParametersLayout`, `InitializersLayout`, `CapturesLayout`;
- `ControlHeadersLayout`, `BaseListsLayout`, `ConstructorListsLayout`,
  `EnumeratorsLayout`;
- `ExpressionsLayout`, `ChainsLayout`, `DeclarationHeadersLayout`;
- `BodiesLayout`, `LambdaBodiesLayout`.

All inherit global `Layout`, except `LambdaBodiesLayout`, which inherits resolved
`BodiesLayout`. Inheritance is compiled once, not looked up per token/group.
Declaration braces use declaration-header ownership; control/case braces use
control-header ownership. Lambda opening braces use declaration-header ownership,
while lambda body topology uses `LambdaBodiesLayout` independently.

When a complete declaration header is actually rebuilt, the boundary from its
complete type/specifiers to a direct named declarator has a one-space flat
fallback, including template return types. This is not a universal space after
`>`: flat-only declarations and preserved headers retain source spacing, and
uncertain or protected syntax remains unchanged. Pointer/reference binding,
nested/member-pointer, operator-name and attribute spacing keep their specific
rules; a type-to-entry fallback never rewrites a pointer-to-name gap.

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
| `ListBreak` | `IfNeeded` (default), `Always` |
| `ListPacking` | `Fill` (default), `VerticalWhenBroken` |
| `ListFirstItem` | `InlineIfFits` (default), `NextLine` |
| `ListCloser` | `SameLine` (default), `OwnLine` |
| `ListAlignment` | `Block` (default), `Delimiter` |
| `ContinuationOffset` | 0 (use IndentWidth), or 1..32 |
| `AlignmentFallbackOffset` | 0 (use IndentWidth), or 1..32 |

For each list category (`Calls`, `Parameters`, `Initializers`, `Captures`,
`ControlHeaders`, `BaseLists`, `ConstructorLists`, `Enumerators`), prefix `Break`,
`Packing`, `FirstItem`,
`Alignment`, `ContinuationOffset`, or `AlignmentFallbackOffset` with that category
name. Delimiter-bearing categories (Calls, Parameters, Initializers, Captures,
ControlHeaders, Enumerators) additionally accept `Closer`. These overrides accept `Inherit`
(the default). Explicit zero offsets resolve to `IndentWidth`, not to the global
offset.

A family `Break` override inherits `ListBreak`. Chains/expressions independently
default to `IfNeeded`, with no list inheritance. A fitting singleton with `Always`
can break after its opener even under `InlineIfFits` and `SameLine` (inline placement
is permission, not a requirement). A genuinely empty list has no item boundary.

First/closer placement applies **only to a broken group**. `Fill` packs complete
items; `VerticalWhenBroken` gives each item a boundary break. Child groups and
comments may require additional breaks inside an item. `InlineIfFits` permits the
first item beside the opener only when it and required suffix can fit;
`NextLine` puts it below the opener. `OwnLine` puts a broken closer on the enclosing
block column; `SameLine` keeps it with the last item unless a hard boundary (e.g.
a line comment) forbids that. Base/constructor lists have no closing delimiter;
`BaseListsCloser` and `ConstructorListsCloser` are rejected with migration advice.
Use `ClassBracePlacement` or `FunctionBracePlacement` for the following body brace.
The internal shared ListPolicy has a fixed SameLine closer for these categories;
it is not a user-visible formatting control.

`Block` anchors continuation at block column + offset. `Delimiter` aligns after
the opener, with block + offset as the minimum. When alignment leaves no room for
the next indivisible atom and required suffix, fall back to block + fallback
offset. Alignment uses rendered columns, not copied source columns. LLVM and
Google explicitly use delimiter alignment with a 4-column continuation/fallback;
repository/YTsaurus examples use block alignment. Expressions use block
continuation; `ChainsAlignment = Delimiter` anchors to the rendered chain root.

**Integration capability gap:** the current engine falls back only when the
anchor reaches Width. The policy contract requires room for the next indivisible
atom **plus required suffix**, even when the anchor itself is below Width.
Lowering/engine integration must implement and test this before claiming complete
LLVM/Google alignment support; the config compiler does not solve this check.

`BaseListsColonSide`, `BaseListsCommaSide`, `ConstructorListsColonSide` and
`ConstructorListsCommaSide` accept `Leading` (default) or `Trailing`. They decide
which side of a selected boundary owns punctuation, not whether a list breaks.
Colon gaps have category-prefixed `ColonSpaceBefore/After` keys; list commas use
shared `CommaSpaceBefore/After` policies.

## Gaps, braces, bodies and commas

Every syntax spacing field now has the same `GapPolicy` values:

| Value | Meaning |
| --- | --- |
| `Preserve` | Retain available source horizontal spacing on preserved/genuinely flat-only boundaries; at canonical topology-changing boundaries use syntax-safe flat fallback before fitting, even if selected flat. |
| `Space` | Prefer one horizontal space on a flat gap. |
| `NoSpace` | Prefer no horizontal space on a flat gap. |
| `GeneratedSpace` | Space when the owning canonical combinator rebuilds the property. |
| `GeneratedNoSpace` | NoSpace when the owning canonical combinator rebuilds the property. |

Break decisions are owned by layout policies, not gap-spacing values. Generated
layout ownership and source-token provenance are separate axes. A canonical
call-list rebuild uses generated edge/separator spacing even where the same token
adjacency existed in source. A nested argument edit does **not** activate generated
policies on a preserved outer call's gaps. Inserted gaps always qualify as rebuilt.
Lowering supplies explicit `GapContext = Preserved | Rebuilt` for each owned
property; it must never infer this from before/after text or arbitrary changes to
an enclosing construct. `Preserve`'s topology exception is determined structurally,
not from final brokenness, token provenance or text history. Discardable tabs and
other horizontal spelling must not affect canonical topology fitting or require a
retry/cleanup pass. Intentionally preserved flat-only properties can affect width.
Lexical separation, line comments, hard boundaries and
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
`EnumBracePlacement` defaults to Attached under DeclarationHeadersLayout;
`ControlBracePlacement` replaces `ControlBlockBracePlacement`.

`BodyShape = Expanded | CompactIfFits` controls **nonempty** bodies, default
Expanded. Independent `EmptyBodyShape` has the same values/default. Lambda shapes
inherit separately: `LambdaBodyShape` from BodyShape, `LambdaEmptyBodyShape` from
EmptyBodyShape, never from LambdaBodyShape. Shape only controls canonical bodies;
preserved topology/protection win. Ordinary compound/control/case/function/lambda,
record and namespace bodies participate; enums/lists and requirement bodies do not.
An empty body has no interior tokens/comments after safe authorized transforms;
comment-only bodies and retained null statements are nonempty. CompactIfFits
keeps interior/closer on one line when safe/fitting but does not move the separately
governed opening brace.

Block, member, access-label, case-label and statement-attribute indentation remain
orthogonal. `IndentNamespaces = Preserve | Indent | NoIndent` defaults to Preserve.
`NamespaceIndentWidth = 0..32` defaults to zero, resolving to IndentWidth only for
Indent. NoIndent means zero relative indent at the enclosing rendered structural
base, not the source brace column. Nested namespaces start from their parent's
content base; `namespace A::B` has one body/application. Ordinary function/control
indentation is unchanged. Comments follow syntax attachment.

Enum header braces are independent of enumerator layout: NextLineIfHeaderBroken
observes the header, never enumerator brokenness. `Enumerators*` uses shared
list rules for scoped/unscoped enums, attributes and underlying types recognized
by syntax; initializer expressions remain child groups. Comment-free canonical
empty enums stay flat even with Always: there is no owned item boundary. Existing
enum trailing commas are preserved, included in fitting, and never select shape;
InitializersTrailingComma does not apply to enums.

`InitializersTrailingComma = Preserve | Never | WhenMultiline` is an explicit
structural action at grammar-approved sites only. `WhenMultiline` removes the
comma in flat output and adds it in multiline output. `Preserve` is the default.
There is deliberately no always-add mode. Reserve optional-comma width during
layout, then choose punctuation from the resulting shape. **Source commas never
select line shape or closer placement.** This removes the old
`SameLineUnlessTrailingComma` cycle.

## Structural vertical gaps

All counts/caps accept `Preserve` or `0..10000`, default Preserve.
`MemberDefinitionBlankLines` additionally accepts/defaults to Inherit, resolving
from DefinitionBlankLines rather than adding to it.

| Key | Syntax boundary |
| --- | --- |
| `NamespaceBlankLinesAfterOpen`, `NamespaceBlankLinesBeforeClose` | Edges of nonempty namespace content, outside attached first/last content comments. Empty bodies have no item edge; comment-only bodies are nonempty. |
| `AccessSectionBlankLinesBefore` | Before subsequent explicit access sections, not the first at body opening. |
| `AccessSectionBlankLinesAfterLabel` | After the label and its same-line trailing comment, before following section content; empty sections have no phantom content boundary. |
| `DefinitionBlankLines` | Between neighboring complete declaration items in file/namespace/record sequences if at least one is a function, record or enum definition. |
| `MemberDefinitionBlankLines` | The same rule inside records, replacing general definition counts. |
| `MaxEmptyLines` | Upper bound on existing excess empty lines in editable vertical gaps. |

Counts mean **empty lines**, not newline characters: a broken boundary with n
empty lines emits n+1 newline sequences. A positive exact count requires an
applicable boundary break independently of surrounding Preserve; zero removes
surplus empty lines but does not itself force/remove the selected boundary break.
Neighboring groups are not reflowed by this narrow override. Preserve adds no
exact count or break requirement.

Two variables/forward declarations do not qualify for definition separation;
lambdas, namespaces and access labels are not qualifying definition items.
Attributes, template prefixes and leading documentation stay with their declaration;
trailing comments stay with the preceding complete item, including its terminating
semicolon. Directives/protection and unattached comments are adjacency barriers,
not gaps through which declarations can be inferred or comments moved.

MaxEmptyLines does not force inline boundaries to break, delete mandatory
line-comment terminators, or change protected/token-interior newlines. An explicit
cap rejects any explicit/resolved exact count above it, including inherited member
counts, after overrides and independently of input/Layout. No silent clamping,
specificity workaround or output cleanup pass is allowed.

Lowering selects one syntax-owned vertical gap; the generic renderer emits its
newline count and indentation only after the final newline. Token-owned CR
LF-first completion affects only the first newline; remaining newline sequences
use global encoding. These constraints belong to lowering/rendering, not a postpass.

## Expressions, chains and width diagnostics

`BinaryOperatorSide`, `AssignmentOperatorSide`, `ShiftOperatorSide` and
`TernaryOperatorSide` all accept `Leading | Trailing`. Shift is a syntax category,
not an operator spelling list. Defaults are trailing binary/assignment/shift and
leading ternary. `ExpressionsBreak = IfNeeded | Always` defaults to IfNeeded.
`ExpressionsPacking = Fill | VerticalWhenBroken` defaults to Fill. Precedence- and
associativity-aware owned operator sequences pack complete expression segments;
operand subtrees remain child groups (the multiplication in `a + b * c + d` is
not flattened into the outer addition sequence). Operator side is independent.
`TernaryLayout = Flexible | ThreePart`; `ThreePart` couples both
ternary boundaries when broken, including under Fill, but alone does not force a
fitting ternary to break. `ChainsBreak = IfNeeded | Always` defaults to IfNeeded.
`ChainsPacking = Fill | VerticalWhenBroken` and `ChainsAlignment = Block | Delimiter`
apply to safely parsed fluent chains selected by library annotations and resolved
ChainsLayout. Links include member calls through `.` / `->` and repeated
call-result invocation such as `factory()(arg)(next)`, not calls inside arguments.

`WidthDiagnostics = Off | Report` replaces `LineLengthCheck`. Report compares
emitted line widths to `ColumnLimit`, including unavoidable/protected overflows.
There is no Soft mode and no extra format-to-test-feasibility pass. Defaults remain
IndentWidth=4, ColumnLimit=120, WidthDiagnostics=Off; limits are 1..32 and 1..10000.

## Migration map and deliberate differences

| Old option family | Replacement |
| --- | --- |
| `Enforce*` | Canonical category layout for shape; typed source/generated gap values for spacing. |
| `MergeFunctionParameters`, `MergeInitializerList` | IfNeeded canonical groups flatten when they fit; Preserve retains topology. |
| `FunctionParameter*`, first function parameter | `Parameters*`; Pack -> Fill, OnePerLine -> VerticalWhenBroken; NameLine/NameLineIfFits -> InlineIfFits. |
| `CallArgument*`, first/sole argument, call closing parenthesis | `Calls*`; every arity follows the shared rule. |
| `InitializerList*`, first initializer element | `Initializers*`; separate closer from trailing comma. |
| `InheritanceList*`, `ConstructorInitializerList*` | `BaseLists*`, `ConstructorLists*`; Format -> Canonical. |
| `*Indent = Continuation` | `*Alignment = Delimiter`, with explicit fallback offset. |
| `*Indent = IndentWidth` | `*Alignment = Block`, with continuation offset. |
| `ControlHeaderClauseSplit`, `ControlConditionIndent`, `ForHeaderClauseIndent`, multiline control positions | Shared `ControlHeaders*`; no separate for-clause indentation exception. |
| `ExpressionLayoutCondition` Overflow/FitOrOverflow | `ExpressionsLayout = Canonical`, `ExpressionsBreak = IfNeeded`; no source-history activation modes. |
| `ExpressionLayoutCondition` Always | `ExpressionsLayout = Canonical`, `ExpressionsBreak = Always`; a legal owned boundary is required, not merely activation. |
| `TernarySplit` AsNeeded/ThreeLines | `TernaryLayout` Flexible/ThreePart. |
| Leading/trailing operator spelling lists | Shared operator-side categories, including `ShiftOperatorSide`; arbitrary spelling overrides are not supported. |
| Chained-call split/layout/indent | `ChainsLayout/Break/Packing/Alignment`; old Always -> ChainsBreak Always, Overflow -> IfNeeded; participation selected by ChainedCallRootNames annotations. |
| `IndentBlockBodies`, nonempty/closing-brace body flags | `BodiesLayout`, `BodyShape`; generic nonempty expanded or compact-if-fits bodies. |
| `CollapseEmptyFunctionBodies` | `BodiesLayout = Canonical`, `EmptyBodyShape = CompactIfFits` for true; Expanded for false. Does not change nonempty BodyShape. |
| `SingleStatementLambdaBodyLayout` | `LambdaBodiesLayout`, `LambdaBodyShape`; use Preserve explicitly to retain source topology. |
| `CommaSpacing`, colon/arrow/overload compound spacing | Before/After GapPolicy keys. |
| `LineLengthCheck` Strict/Soft | `WidthDiagnostics = Report`; unavoidable overflow is reported too. |

**Explicitly unsupported:** `CallArgumentSplitColumnLimit` and
`CallArgumentSplitColumnLimitNames`: all calls use the global ColumnLimit; there
are no named-width inputs or overrides. `SeparatorLength` (rewriting comment
spelling) and `PreprocessorIndentation` (rewriting protected directives) also
remain unsupported. Exactly three explicit library-aware layout annotations are
retained below; semantic whitespace-sensitive macro protection remains separate.

Safe explicit structural settings remain: include ordering, namespace comments,
missing control braces, null statements/declarations and empty control bodies.
Their syntax/safety checks remain mandatory; the config schema is not permission
to transform unknown or protected syntax. Naming/prefix/parameter diagnostics and
file exclusions remain independent.

Profiles explicitly opt into Canonical with IfNeeded; fitting normalization is
conditional on that break requirement. They are approximate styles, not
byte-for-byte upstream presets: fitting source multiline constructs can flatten
unless Always requires a legal owned break,
safe nested groups participate, YTsaurus message/chain-root/visual-scope annotations
are retained, named EXPECT_THROW width overrides are removed, control clauses
share one anchor policy, and commas do not
drive closer placement. LLVM/Google context-dependent base/constructor lists use
explicit Preserve overrides. All existing fixture `.hit-fmt` files have been
translated where necessary; no output golden was regenerated. Migration headers
mark deliberate differences; fixture-output review belongs to integration.

## Exactly three library-aware layout annotations

The familiar flat keys remain, now as explicit library metadata rather than
renderer name checks or sequential-pass triggers:

- `CallLineFirstStringNames = ["MESSAGE_CALL", ...]`: exact identifier annotations
  select the typed `PinCompleteStringLiteral` first-argument override. Lowering
  may pin the first item only if the **complete first argument expression** is a
  string-literal expression (including literal concatenation), not a prefix of a
  larger expression or a condition. Comments, protection and mandatory boundaries
  override the pin, as does an unsatisfied explicit Always break requirement. An
  annotation does not implicitly switch preserved category
  layout to Canonical. No message-name heuristics or per-name widths are allowed.
- `ChainedCallRootNames = ["factory", "ns::Builder", ...]`: an empty list selects
  `AllSafe`; a nonempty list selects `AnnotatedRoots`. Both are independently
  gated by resolved ChainsLayout: Preserve retains topology, Canonical enables
  the generic chain policy for participating roots. This empty-list rule is a
  deliberate breaking semantic. Lowering composes the chain spine from
  `MemberExpression` receiver links and `CallExpression` **callee links**: member
  calls through `.` and `->`, plus repeated result invocation
  `factory()(arg)(next)`, including mixed spines. It must not follow call-argument
  edges into the spine: calls inside arguments are child groups, not chain links.
  The selected annotation feeds the same reusable generic chain/group policy.
- `IndentedCallScopePairs = ["BeginList:EndList", ...]`: fixed pairs select the
  typed `PreserveExistingExtraLevel` visual-scope annotation. Lowering matches
  balanced pairs only within the **same parsed statement sequence, receiver and
  wrapper context**. Preserve an existing extra visual indentation level; do not
  invent an extra level merely from a method spelling. Unmatched, ambiguous or
  preprocessor-boundary-crossing scope regions remain unchanged. No inferred
  Begin/End naming convention, matcher DSL or per-token regex is permitted.

All names are case-sensitive exact spellings. Message/scope names must be simple
identifiers; chain roots may be qualified identifier paths. Empty/invalid names,
duplicates, same-name Begin/End pairs and reused/ambiguous scope endpoints are
rejected deterministically. The three YTsaurus annotation sets from the base
profile are retained: 17 message names, 15 fluent roots and three scope pairs.

`formatter/config_annotations.hpp` defines the separately owned metadata API:

```cpp
void ValidateLibraryAnnotationStyle(const Style&);
LibraryAnnotations CompileLibraryAnnotations(const Style&);
```

Compilation validates and owns sorted contiguous records. LibraryAnnotations
exposes read-only `MessagePins()`, `FindMessagePin(name)`, `Chains()`,
`IsAnnotatedChainRoot(name)`, `ChainRoots()`, `VisualScopes()`,
`FindScopeBegin(name)` and `FindScopeEnd(name)`; there are no mutable record
accessors. Scope-end lookup uses a separately sorted integer index into owned
scope records, so copies do not retain self-referential views. Records use FirstArgumentAnnotation,
ChainParticipation and VisualScopeLayout typed enums. Lookups allocate no memory
and never use regex; rendering must not receive the names.

LoadConfigFile validates raw annotation metadata, but **Configuration does not
cache a derived annotation snapshot alongside mutable Style**. Callers explicitly
compile the final Style after all overrides, own the result as const and reuse it
across files alongside CompiledPolicies. Mutating raw Style cannot invalidate
annotation strings: the snapshot owns its own names. Spans/pointers returned from
it remain valid until that snapshot is moved/replaced/destroyed. Source/AST references used
by contextual lowering must instead have input-file/arena lifetimes; they must
never be retained in this configuration-lifetime metadata.

Scalar CompiledPolicies remains immutable and allocation-free; annotation
compilation may allocate once for configuration-lifetime names/records. The
config component does not implement syntax matching or layout lowering. Lowering
selects generic typed pin/chain/nesting overrides with the contextual guards above;
the engine knows no identifiers. Semantic macro syntax/protection hints are an
independent safety contract, not a fourth layout annotation feature.

## Library contract and integration registration

`formatter/policy.hpp` is parser/engine-independent. Public APIs:

- `CompiledPolicies CompilePolicies(const Style&)` validates dimensions, resolves
  inheritance/zero offsets and returns a trivially copyable immutable snapshot.
  Compile once per final style and reuse it across files; do not recompile per
  token/group. Mutating raw Style requires compiling a new snapshot.
- `void ValidatePolicyStyle(const Style&)` validates raw programmatic dimensions,
  blank counts/caps and resolved exact-count conflicts; both APIs throw
  `std::invalid_argument` for invalid settings. Config validation occurs after all
  file overrides; diagnostics include conflicting names and available setting
  locations. CLI callers must revalidate/compile after final overrides.
- `constexpr std::optional<bool> GapUsesSpace(GapPolicy, GapContext) noexcept`:
  true means Space, false means NoSpace, disengaged means preserve/fallback.
  `GapContext` is `Preserved | Rebuilt`, supplied explicitly by the property's
  owning lowering combinator, independently of token provenance. Inserted gaps
  always use Rebuilt; unrelated nested edits never activate preserved owners.
- `std::string_view ConfigMigrationHint(std::string_view)` returns advice only;
  it never accepts an old option.

`CompiledPolicies` exposes const Layout, IndentWidth, ColumnLimit,
WidthDiagnostics, IndentationKind, TrailingWhitespace,
PointerReferenceDeclaratorLocation; IndentationControls Indentation;
ListPolicy Calls/Parameters/Initializers/Captures/ControlHeaders;
HeaderListPolicy BaseLists/ConstructorLists; ExpressionPolicy Expressions;
ChainPolicy Chains; DeclarationHeaders mode; BodyPolicy Bodies/LambdaBodies;
BracePolicy Braces (including Enum); InitializersTrailingComma; GapPolicies Spacing;
ListPolicy Enumerators; VerticalGapPolicies VerticalGaps.
`ListPolicy` contains Layout, Packing, FirstItem, Closer, Indent and Break.
ExpressionPolicy additionally carries Break and Packing; ChainPolicy carries Break.
BodyPolicy carries independent Shape/EmptyShape and resolved IndentWidth.
`IndentationPolicy` contains Alignment, ContinuationOffset and FallbackOffset.
`IndentationControls` contains AccessModifiers, Members, CaseLabels
(`BodyIndentControl`), StandaloneBraces (`StandaloneBraceIndentControl`) and
StatementAttributes (`AttributeIndentControl`), and Namespaces (`BodyIndentControl`). Each contains its typed Mode and
a one-byte Offset. All offsets resolve to 1..32: zero access/member/case/attribute
and namespace widths use IndentWidth; standalone braces use IndentWidth directly. Mode decides
whether to apply the offset, align to the block, or retain source. Both body
policies already carry resolved BlockBodyIndentWidth. No retained scalar layout,
spacing, indentation, declarator, or text-output control requires raw Style during
layout lowering. Other policy members are defined exhaustively in `policy.hpp`.

`config_vertical.hpp` defines raw `BlankLineSetting{BlankLineMode Mode,
size_t EmptyLines}` (Preserve/Inherit/Exact, with Inherit allowed only for member
definitions). Compiled `VerticalGapPolicy{VerticalGapMode Mode, uint16_t EmptyLines}`
contains only Preserve/Exact. `EmptyLineCap{bool Enabled, uint16_t EmptyLines}`
represents the optional cap without a runtime sentinel. `VerticalGapPolicies`
exposes the six exact-count fields and cap under their exact source key names.
All counts are checked before narrowing. BreakRequirement is IfNeeded/Always;
there is no Inherit in any compiled policy.

Separate raw-Style contract: HonorClangFormatDirectives, CallLikeMacroNames and
WhitespaceSensitiveMacros are semantic safety/syntax metadata. IncludeOrder,
AddMissingControlBraces, RemoveNullStatements, RemoveNullDeclarations,
EmptyControlBody, NamespaceComments, NamespaceCommentSpaces and ShortNamespaceLines
belong to the explicitly opted-in structural-transform stage. That stage may read
the final raw Style; it must obey syntax/protection constraints before lowering.
Naming/parameter diagnostics and Exclude remain in Configuration, not layout.

The existing `LoadConfigFile(path)` still returns raw `Configuration`, preserving
naming/exclusion metadata outside the layout snapshot. Integration should call
CompilePolicies after its final CLI overrides, then pass the snapshot to syntax
lowering. This avoids a stale implicit cache in mutable Configuration.

Integration must add `formatter/policy.cpp` to `lib-formatter` and
`formatter/tests/config_policy.cpp` to the GoogleTest executable. The new tests use
the already registered FORMATTER_TESTDATA_DIR definition and library/GTest links.
The new template round-trip test also needs `hit-fmt/config_template.cpp` linked
into the test executable; it calls library helpers, never the CLI.
No extra compiler dependency or runtime allocation facility is required (C++20).
The old formatter/CLI/tests temporarily reference removed fields; that is an
intentional handoff boundary, not a reason to restore legacy flags.

## Configuration consumer status

The schema accepts these eight keys; standalone-comment counts are implemented,
while the other nondefault consumers remain staged:

| Key | Values | Default |
| --- | --- | --- |
| StandaloneCommentBlankLinesBefore | Preserve or 0..10000 | Preserve |
| StandaloneCommentBlankLinesAfter | Preserve or 0..10000 | Preserve |
| BreakAfterTemplateHeader | false or true | false |
| ExpressionsAlignment | Block or Operand | Block |
| IncludeMatchingHeader | Off or First | Off |
| IncludeGrouping | Preserve or Regroup | Preserve |
| IncludeGroupPrefixes | ordered quoted directory-prefix array | [] |
| ConstructorBracePlacement | Inherit, Attached, NextLine, NextLineIfHeaderBroken | Inherit |

Low-level CompilePolicies resolves immutable scalar metadata, including
ConstructorBracePlacement from the final FunctionBracePlacement once. Prefix
metadata is separately configuration-owned by PreparedIncludePrefixes: no Style
views, input paths, per-file compilation or mutable caches. Its lookup accepts
already-normalized relative slash paths and returns the earliest configured
matching ordinal (not the longest); RuleCount denotes unmatched. Prefixes must
end in `/`, with no empty, `.` or `..` components, absolute paths, backslashes or
duplicates. Backslashes in input include names remain ordinary bytes: engine/
item\name.h matches engine/, but engine\item.h does not. This is a rule-syntax
restriction, not a reinterpretation of input path bytes. Overlapping prefixes are valid. Inactive prefixes do not enable
regrouping. IncludeOrder still defaults to Preserve; only explicit Sort sorts.

Explicit standalone-comment counts must fit an explicit MaxEmptyLines cap.
Regroup requires one empty line and conflicts with MaxEmptyLines=0 independently
of input, Layout or IncludeOrder. Validation runs after final overrides and
before public preparation's pending-consumer rejection.

Standalone-comment counts own only external gaps of maximal own-line comment
blocks in declaration, statement and member sequences. Before and After default
to no override (Preserve); explicit After wins over Before on a shared boundary,
then existing boundary policies apply. Internal comment whitespace and bytes,
attachment, inline/trailing comments and list/expression interiors are unchanged.
Safety and edit scope win; file start/end do not acquire phantom edges. Positive
counts force only the owned boundary to break; zero retains required topology.
Exact counts are validated against MaxEmptyLines, with no cap exemption.

Public PreparedFormat rejects the remaining nondefault staged behavior by setting name;
it must not silently ignore a staged option. Inactive prefixes may validate, but
they cannot change output. Remove each isolated guard clause only after the
corresponding template/expression/constructor/include consumer has been
reviewed and integrated. Matching additionally needs the reviewed original-file
context boundary; metadata alone is not matching or grouping support.

ColumnLimit remains 1..10000, default 120, and zero is still rejected. After the
reviewed dynamic renderer and width-zero consumer R integrate, the activation
patch must change ValidatePolicyStyle's lower bound to accept zero, change
IntegerValues("ColumnLimit") help to `0 (unlimited) or 1..10000`, and document its
no-width-driven-wrap/no-width-diagnostic meaning. Only then apply the separately
approved zero-invalid witness disposition (10001) and real mandatory-break,
saturation and finite/zero behavior coverage. This stage does not implement 18.
