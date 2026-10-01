# Parser-independent layout engine

`formatter/layout.hpp` exposes `formatter::layout::Document`. It has no parser,
configuration, CLI or legacy formatter dependency. Syntax lowering owns lexical
separation, structural-edit authorization, comment attachment and protected
range discovery; this engine never guesses them from token spellings.

## Construction and ownership

Create `Document(source, fileResource)`, optionally `Reserve(nodeCapacity,
groupCapacity)`, append nodes, then `Seal()`. All text is borrowed: the original
source, generated literal views and PMR resource must remain alive through every
render; borrowed source/literal bytes must also remain unchanged. Generated strings are **not** copied into document storage. Input byte
size and node counts must be below `UINT32_MAX`; IDs are unsigned 32-bit integers
with `InvalidId` as the sentinel. Groups are numbered in opening order.

- `BeginGroup(GroupSpec{Mode, Indent, ForceBreak})` / `EndGroup(id)` form a strictly nested
  stream. `Consistent` breaks all its own soft gaps together. `Fill` greedily
  packs segments up to the next available soft boundary. Nested groups decide
  independently inside a broken parent; a flat parent forces children flat.
- `Text(view, provenance)` is single-line generated/rewritten text.
  `SourceText(begin, end, identity, verbatim=false)` borrows exact source bytes;
  its offsets must be real (sentinels are valid only for generated provenance).
  `Verbatim(view, provenance)` is opaque, possibly multiline text. If provenance
  supplies a source range, the verbatim bytes must exactly equal that slice.
- `Gap(GapSpec{Kind, Spaces, RequiredWidth, LineEnding, NewlineCount,
  FlatSourceBegin, FlatSourceEnd, IndentSourceBegin, IndentSourceEnd}, anchor,
  provenance)`
  has flat, soft or hard behavior. Flat gaps never break, hard gaps always emit a newline, soft
  gaps belong to the current group. Root soft gaps stay flat. A newline replaces
  the flat spaces and includes generated indentation. Hard gaps are not silently
  collapsed next to opaque newlines: lowering must choose boundary ownership.
  RequiredWidth is a per-gap column demand used only for delimiter alignment on
  an emitted newline; it does not alter flat widths or choose whether to break.
  Optional FlatSourceBegin/End offsets both absent (`InvalidId`) retain Spaces;
  both present replace flat spaces with that exact source slice, even if empty.
  Only ASCII space/tab is valid. Partial, reversed, out-of-bounds or nonhorizontal
  spans reject with sticky InvalidSourceRange before slicing. Source offsets, not
  owning strings/views, are stored. Each ConditionalGap selects its own slice;
  newline emission suppresses the slice completely. Literal-tab fit is conservative,
  but emission advances actual tab stops. Flat maps use the exact selected span
  and original origin identity; newline maps retain original origin with Exact=false.
- `MarkAnchor(AnchorSpec{Kind, Offset, Fallback, Minimum})` captures current
  generated block indentation and current **output** column before any use.
  Block-relative anchors add signed Offset to the captured block. Delimiter
  anchors add signed Offset to the captured output column, then resolve minimum
  and fit separately at each newline gap. Group indentation adds its signed
  offset to its parent's indentation. Anchors can be reused for different gaps;
  their stable block base does not become a later group's continuation indent.
  Signed offsets clamp to `[0, UINT32_MAX-1]`.
- `ConditionalText(owner, flat, broken, provenance, fitWidthFloor=0)` and
  `ConditionalGap(owner, flat, broken, anchor, provenance)` couple punctuation or
  spacing to their **current owning group**. References to ancestors,
  descendants, closed siblings or future groups are rejected. Text alternatives
  must be single-line and the conditional gap's flat branch **must be Flat**;
  its broken branch may be Flat, Soft or Hard. These constraints avoid shape
  cycles and context-dependent width rescans. The unsigned fitWidthFloor reserves
  `max(measured flat width, floor)`, never an additive width; broken measurement
  uses actual broken spelling. It changes no output bytes, padding, columns or
  mappings, and cannot alone make an unbreakable owner Broken. Optional commas
  can use floor 1 whether or not the original source had a comma.

Builder errors are sticky `Status{Code, Node}` values. `Seal` checks unfinished
nesting; malformed IDs/ranges/dependencies are rejected at append time. Rendering
an invalid or unsealed document returns an empty error result. Mutation after
sealing invalidates the document. Allocation failure propagates `std::bad_alloc`;
callers can use a bounded PMR resource. There is no global storage or depth limit.

## Layout invariants

`Render(Policy{Width, TabWidth, LineEnding, SourceMap, Indentation}, resultResource)`
returns
`Result{State, Text, Map, Broken}`. Repeated calls are independent and deterministic;
no prior rendering, source-byte difference or whitespace history influences a
decision. Result storage uses `resultResource`, which must outlive the result.
Do not reset a scratch resource while its returned text/map/decisions remain live.

One bottom-up pass caches saturating flat widths, first-line suffix prefixes and
eligible break sites; one explicit-stack traversal chooses layout and emits text.
There is no recursive walk, suffix rescan, search, reparsing or rerender loop.
Time is O(nodes + borrowed text bytes + output bytes), memory O(nodes + depth +
output bytes). PMR contiguous vectors hold fixed-size records, stacks, mappings
and decisions. Reserve document capacity when token counts are known, especially
with a monotonic arena. Release the arena when that input file finishes.

A fit includes group contents, its closers, and the relevant same-line suffix
through enclosing groups. In broken fill mode, the next soft boundary ends the
segment's suffix obligation. Own-group flat alternatives participate in flat
measurement; the broken punctuation of an enclosing group participates in its
children's actual suffix obligations. Long indivisible atoms may overflow: an
unbreakable one-line group does **not** claim to be broken merely for overflowing.
Hard/opaque child lines force the enclosing output shape to be multiline. Such
children contribute their broken conditional prefix to enclosing suffix fits,
including conditional text and Flat-gap spacing. Potentially flat children still
contribute their flat summary; this distinction is computed once bottom-up.

`Broken[id]` describes actual multiline group output, not just fit rejection.
If expansion is selected without an unconditional line, the cached last eligible
break ensures at least one newline. Fill otherwise uses its normal greedy breaks;
if no earlier line occurs, it takes that last site. A required shape can propagate
into the descendant owning that site, but only an explicitly editable soft gap or
broken conditional gap is eligible. Flat gaps and protected text are never forced
apart. This also handles a flat conditional suffix that becomes shorter in the
broken alternative without claiming a nonexistent output line.

### Columns and encoding

Width accepts `1..UINT32_MAX-1`. Width arithmetic saturates at `UINT32_MAX`.
Columns count bytes, not Unicode display cells; ordinary non-tab bytes consume
one column. `TabWidth` accepts `1..32`: emitted literal tabs advance to tab stops.
Cached text widths conservatively charge each tab its full tab width, so input
with literal tabs can break earlier than an exact tab-stop fit. This conservatism
never applies to generated indentation. `Policy::Indentation` is Spaces (default)
or Tabs: a newline gap emits either exactly the target number of spaces, or
`target / TabWidth` tabs followed by `target % TabWidth` spaces. Both produce the
same visual column starting at the new line's column zero, with no post-render
pass. Horizontal flat-gap spacing still uses spaces. LF/CRLF policy applies only
to generated gaps; opaque/source bytes remain unchanged, including CRLF, standalone
CR, tabs and their original indentation. A CR or LF resets the output column.

`GapSpec::LineEnding` is `GapNewline::{Inherit, LF, CRLF}` (default Inherit),
appended after RequiredWidth so earlier aggregate field order is unchanged.
Inherit uses `Policy::LineEnding`, which remains `Newline::{LF, CRLF}` only.
The first emitted newline uses the selected ordinary/ConditionalGap override;
flat gaps emit only their spaces, irrespective of LineEnding. Both conditional
alternatives and all gap kinds validate override values at construction, including
unused/flat alternatives; malformed values yield sticky InvalidPolicy.

Lowering can set LineEnding=LF for `SourceGap.MustStartWithLineFeed`: when an exact
source line-comment/directive token already owns CR, the gap's LF completes CRLF
without adding another CR. Preserve that source token as an exact opaque atom.
Subsequent Inherit gaps still use the global policy. This is per-gap emission,
not source scanning, policy mutation, blanket CRLF freezing or output rewriting.
Newline encoding does not change indentation, fit widths or actual Broken shape.

`GapSpec::NewlineCount` is appended after LineEnding: nonzero uint16, default 1
(valid range 1..65535, validated even in unused/flat specs). A vertical gap emits
N newlines, with indentation only after the final one: blank lines have no spaces
or tabs. The first newline honors the local override; the remaining N-1 use the
GLOBAL policy. Thus an LF-first gap after source-owned CR followed by blank lines
under global CRLF emits LF, then only CRLF, then final indentation. One mapping
spans all actual generated bytes. Flat emission ignores the count.

Optional `IndentSourceBegin/End` offsets are appended after FlatSourceEnd and
use the same absent-pair, bounds and ASCII space/tab validation as flat source.
A selected newline gap with present offsets emits that exact indentation only
AFTER the final newline, instead of any generated indentation. Present-empty
means none; flat emission ignores it. Its visual column starts at zero and
advances actual tab stops, but the configured frame/block base is not changed.
Later delimiter anchors see actual output columns; block anchors keep their
configured base. Lowering selects this only when indentation remains preserved.
Mixed provenance splits that gap into generated-newline mapping (original origin,
Exact=false) and borrowed-indent mapping (selected exact span, same identity),
including an empty exact mapping when appropriate. These associations can overlap
source ranges; edit planning owns atomic gap replacement rather than treating
maps as independent edits.

`GroupSpec::ForceBreak` is appended after Indent, default false. A canonical
owner with a legal editable OWNED site rejects flat layout and requires a break
at an owned boundary, even when a nested child is already multiline. Owned hard
or selected conditional boundaries satisfy it. Consistent groups break all own
soft sites; Fill retains greedy packing and takes its cached last owned site if
no owned break occurs. Forced child multiline/broken-prefix summaries propagate
to enclosing fits. No owned legal site means no ForceBreak-imposed descendant
break; empty/unbreakable owners stay honest and protected/flat atoms stay intact.
Intrinsic child lines still contribute actual Broken, not an owned boundary.

External continuation is automatically derived: cached sibling suffix prefixes
plus the enclosing frame's tail include parent closers and root punctuation.
Terminal unbroken Fill segments include that tail; earlier segments stop at their
next soft boundary. Nothing is folded twice into a parent's cached child width.
There is intentionally NO FitSuffix field/parallel lowering width knob. Nested
`(aa, bb);` fits exactly at 9 and breaks at 8; terminal `[aa, bb, cc];` fits at 13
and breaks only the terminal Fill boundary at 12. Lowering should emit normal
closer/semicolon atoms in the document, not artificial spaces or duplicate hints.

### Delimiter minimum and fit requirement

`AnchorSpec::Minimum` is an unsigned continuation minimum relative to the block
captured at MarkAnchor. The aligned candidate is the larger of the captured output
column plus signed Offset and captured block plus Minimum. Minimum zero still
floors alignment at that block; additions saturate/clamp at UINT32_MAX-1.

`GapSpec::RequiredWidth` is the columns needed by the next indivisible segment
**plus its relevant same-line suffix**. Lowering computes reusable summaries once
and supplies the demand; the renderer never scans following source/documents.
Zero adds no demand. UINT32_MAX can express a saturated, never-fitting demand.
Literal-tab segments may use the same conservative full-tab-width bound as text
summaries; never derive generated layout requirements from original columns.

For each emitted delimiter-aligned newline, if candidate is at/beyond Width or
RequiredWidth exceeds `Width - candidate`, use captured block plus signed
Fallback instead. Equality fits. Minimum applies to the aligned candidate, not
the fallback. The fallback is used once even if it cannot fit: long indivisible
atoms/indentation may overflow without truncation, repeated relocation or search.
For example, column 5 + `beta)` demand 5 exceeds Width 8, so Fallback 2 relative to
block zero yields column 2; a demand of 3 remains aligned at column 5 exactly.

BlockRelative anchors ignore Minimum, Fallback and RequiredWidth. Unanchored gaps
and flat gaps also ignore RequiredWidth. Existing two-field GapSpec, three-field
AnchorSpec and four-field Policy aggregate construction remains valid because
new fields are appended with defaults. Preserve-mode whitespace remains borrowed
source/verbatim text; a generated Preserve indentation preference can normalize
to Spaces rather than introducing a third rendering mode.

## Same-owner composition

A header owner can enclose nested condition/parameter groups, then its conditional
brace gap and opening brace. End the header **before** an unconditional body:
otherwise the body's hard lines intentionally make the header owner multiline.
This works equally inside a lambda/list element; it needs no external dependency.

```cpp
Document doc(source, fileArena);
auto header = doc.BeginGroup();
doc.Text("if (");
auto condition = doc.BeginGroup({GroupMode::Consistent, 2});
doc.Text("left");
doc.Gap();
doc.Text("&& right");
doc.EndGroup(condition);
doc.Text(")");
doc.ConditionalGap(header, {GapKind::Flat, 1}, {GapKind::Hard, 0});
doc.Text("{");
doc.EndGroup(header);
// Append the body as a separate indented group and its closing brace.
```

For a delimited list, mark the closing block anchor before opening its indented
owner; put elements, `ConditionalText(list, "", ",")`, its conditional closing gap
and closer inside that owner. Keep semicolons/other suffixes either inside or after
the group: the fit accounts for them. Only syntax lowering may authorize a trailing
comma (for example an initializer, not a C++ function call).

## Provenance and integration

Each emitted atom/gap, including empty alternatives/deletions, gets one `Mapping`
when SourceMap is enabled, except a newline with borrowed indentation gets separate
generated-prefix and exact-indent mappings. Output half-open offsets are ordered and contiguous;
source half-open offsets and immutable caller-supplied `Identity` are retained,
not rebound token ordinals. `Exact` identifies unchanged borrowed source bytes;
`Protected` identifies opaque atoms. Generated material has invalid source offsets
unless lowering attaches a replaced source range. Gap mappings include generated
newline and indentation. The engine does not reorder source identities or infer
atomic hunk units: lowering/edit planning owns those decisions and must not place
editable gaps inside protected ranges. Mappings support those consumers without
copying per-token strings or losing original positions.

Build registration is intentionally left to the integration owner: add
`formatter/layout.cpp` to `lib-formatter`, `formatter/tests/layout.cpp` to the shared
GoogleTest executable, and define `GROUP_LAYOUT_TESTDATA_DIR` to the absolute path
of `formatter/tests/testdata/group-layout`. The seven engine goldens use the existing
`tests/golden.hpp`; no tests run the CLI. This component alone is not the syntax
builder, profile migration, structural edit stage or replacement Format entrypoint.
