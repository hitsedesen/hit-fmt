# Syntax-owned group architecture proposal

> **Target architecture, not an integrated replacement.** The published branch
> contains the source-protection foundation; its formatter still uses the legacy
> reflow/repair orchestration. Component work does not establish end-to-end
> acceptance. Module names below identify target seams, not published APIs.

This design implements the policy model in the
[configuration proposal](configuration-proposal.md). That document owns the key
catalogue, defaults, inheritance and migration. The contracts below include the
approved policy extensions; an older configuration-document revision may not yet
list them. Neither document promises current executable support.

## 1. Problem, goals and boundaries

The current formatter combines sequential string rewrites, syntax rebinding,
before/after-header snapshots and restoration of protected regions. Layout can
therefore depend on source history or which earlier pass changed nearby text.
Replacing one list reflow helper cannot remove those dependencies.

The target replaces that orchestration with compositional, deterministic groups:
syntax owns boundaries, protection is established before edits, and one layout
solve selects all spacing, breaks, indentation and shape-dependent punctuation.
Canonical owners use syntax, resolved policies, child shapes and one global width;
preserved owners deliberately retain their source topology. Comments and unknown
syntax do not require a whole-file fallback.

Goals are idempotence, local safe formatting, explicit provenance, bounded work,
and configurable YTsaurus/LLVM/Google-like profiles. Non-goals are byte-for-byte
clang-format compatibility, full C++ semantic proof, a user-rule DSL, named width
budgets, compatibility aliases or a new legacy escape path.

## 2. End-to-end flow

```text
configuration + final overrides
        |
        +--> validate/compile --> immutable scalar policies
        |                    \--> separately owning, sorted library annotations
        \--> explicit structural options + syntax/safety metadata

original source --> lossless tokens/tree --> source/protection analysis
                                              + hunk eligibility
                                                    |
                              optional atomic structural plan
                                                    |
                                              token edits?
                      +--------- no ----------------+---- yes (nonempty plan) --+
                      |                                                         |
             reuse original source/tree/analysis          materialize ONCE
                      |                                   parse normalized ONCE
                      |                                   project ORIGINAL protection
                      |                                   compose original identities
                      |                                   rebuild normalized analysis
                      +-------------------------------------------+
                                                                  |
                           gap classification + syntax/annotation lowering
                                                                  |
                       group document --> validation + cached summaries
                                                                  |
                             deterministic solve/emission + source map
                                                                  |
                         optional mapped namespace-comment materialization
                                                                  |
                                 diagnostics + output + authorized hunks
```

Whitespace layout does not trigger reparsing. A structural plan is computed from
the original tree, not applied as a succession of format/reparse/reflow attempts.
With no token edits, reuse the original source, tree and analysis: no normalized
copy or additional parse. A nonempty edit plan materializes atomically once, then
parses the normalized text once.

On that edited branch, rebuild tree/token-indexed source/gap/region analysis for
the normalized tree using projected **original immutable protections** and
composed original identities. Original token/node-indexed tables cannot simply
be reused after insertions, deletions or permutations. This bounded optional
analysis refresh is not another formatting or retry pass; one layout solve remains.

Namespace comments are an explicitly enabled text-semantic action after layout:
resolve original namespace identities against the emitted map, validate, then
materialize once. This is **not** a spacing or blank-line repair pass, does not
reflow output, and does not initiate another parse or solve.

## 3. Components and data contracts

| Target seam | Responsibility and output |
| --- | --- |
| Policy compilation | Validate final overrides and exact-gap/cap conflicts; resolve inheritance and zero offsets once. Produce immutable scalar policies and a separate owning annotation snapshot. No mutable derived configuration cache. |
| Lossless parser | Token spellings, half-open ranges, punctuation roles, matched delimiters, grammar children and shared operator metadata. Source must outlive borrowed spelling views. |
| `source_analysis` foundation | Sorted/merged protection with reasons; lexical separation, required/forbidden line boundaries, context-sensitive gaps and region completeness. Completeness is not semantic authorization. |
| Structural planning/materialization | Explicit typed atomic actions, local skip reasons, original-to-normalized provenance and generated adjacency. No layout decisions. |
| Syntax/gap lowering | Assign ownership, classify Preserved/Rebuilt properties, attach comments and annotations, choose generic groups/anchors and reusable segment summaries. No output-text heuristics. |
| Group core | Validate document dependencies, summarize once, solve deterministically and emit bytes/maps. No parser, configuration names, regexes or library identifiers. |
| Result/hunk adapter | Compose final coordinates, validate namespace actions, report actual output diagnostics, return owned results and honor selected edit scope. No independent feasibility formatting. |

Every emitted token/atom and every gap has one emission owner; nested groups may
observe a child's summary without re-emitting its tokens. One topology owner
controls each break, while an explicit vertical policy may independently own the
same boundary's empty-line count under the configuration contract. Lowering
resolves this into one gap record, not competing writers.

Syntax-derived intervals must form a laminar event forest: siblings are disjoint,
children are contained, and equal-boundary events have deterministic ordering.
Overlapping grammar views cannot each claim the same gap. Validation rejects
crossing ownership, duplicate emission and invalid conditional dependencies.
Ordinary comments retain syntax attachment; hard comment boundaries remain hard.

### Group algebra and a synthesized ownership sketch

The document uses compact source/generated atoms (including opaque verbatim
islands), flat/soft/hard gaps, Fill/consistent groups, rendered-column anchors,
same-owner conditionals, shape-dependent punctuation and provenance. Flat gaps
cannot break; soft gaps are legal choices; hard gaps require a line boundary.
Consistent groups couple applicable owner boundaries; Fill groups pack complete
items or segments without flattening their child syntax.

For this small input, identifiers carry no special formatting meaning:

```cpp
if (Ready(Build(seed, flags))) {
    return output;
}
```

```text
Header H [control-header ownership]
  "if" flat-gap "("
  Call C1: "Ready" "(" Call C2: "Build" "(" seed soft-gap flags ")" ")"
  ")" same-owner conditional-gap(H shape) "{"
END H                         <-- before any body hard line
Body B [content + closing brace; opening brace already emitted by H]
  hard-gap(block-anchor + indent) "return output;"
  hard-gap(enclosing-block-anchor) "}"
END B
```

The sketch elides comma atoms/spacing but not ownership: C2 owns its item gaps,
C1 owns its argument edges, and H owns the header-to-opening-brace boundary.
The call groups stay children, not copied token streams. H ends before body hard
lines, so a multiline body does not manufacture a broken header or activate
`NextLineIfHeaderBroken`. The brace boundary observes H through a same-owner
conditional; it does not inspect an unrelated sibling body or a text snapshot.

## 4. Fit, shape and indentation

There is exactly one `globalColumnLimit` budget, resolved from `ColumnLimit`.
`Broken` means actual multiline output, including hard lines and multiline child
atoms; it is not a policy flag. Parent fit accounts for child decisions without
semantically flattening their subtrees. Unbreakable/protected overflow is emitted
intact and, when enabled, diagnosed from the emitted widths.

Break requirement and packing are independent:

- `IfNeeded` permits a fitting canonical owner to stay flat.
- `Always` selects an available legal editable boundary owned by that nonempty
  group, even if a child already renders multiline. A child break cannot replace
  an available required owner break. Protection, preserved ownership and hard
  constraints win; empty, indivisible or wholly unbreakable groups cannot invent
  breaks or a fake `Broken` state.
- `Fill` packs complete owned items/segments after that decision;
  `VerticalWhenBroken` breaks applicable owned boundaries. First-item and closer
  preferences apply in the broken shape. A fitting singleton can break after its
  opener under `Always`; an empty list has no item boundary.

Expressions use the parser's shared precedence/associativity metadata. For
`a + b * c + d`, the multiplication is an operand child of the additive sequence,
not extra outer items. Explicit parentheses remain explicit. Operator-side
policies are orthogonal; `ThreePart` couples both ternary boundaries once that
ternary breaks, but does not alone force a fitting ternary to break.

Optional initializer punctuation reserves its flat-fit width before shape is
selected, then emits only the selected punctuation. Reservation changes fit
decisions, **not output padding**. Existing enum trailing commas count as source
atoms; they neither select brokenness nor invoke initializer-comma transforms.

Ancestor/sibling suffix obligations are already derived by the renderer through
`Frame.Tail`. Do **not** add `GroupSpec.FitSuffix` or a duplicate lowerer suffix
budget. Lowering supplies `RequiredWidth` only for the next indivisible segment
plus necessary same-line suffix when a delimiter anchor needs fallback. Compute
and reuse segment summaries; this demand is not added again to flat widths and
does not itself force a break.

Anchors use rendered columns and the enclosing block base captured when marked.
Delimiter alignment applies its continuation minimum; if the next segment plus
necessary suffix cannot fit, select the configured block-relative fallback once.
Equality fits. If the fallback still overflows, emit intact, without retries.
Generated indentation uses explicit spaces/tabs and configured tab stops, never
an opening brace's historical source column.

## 5. Policy extensions are lowering, not additional passes

The six approved capabilities reuse the algebra above; see the configuration
proposal for the full key/default table:

1. **Explicit break requirements** for lists, chains and expressions lower to
   legal owned boundaries. Packing does not substitute for force.
2. **Independent empty-body shape** uses the corresponding empty/nonempty global
   or lambda policy. Empty means no interior tokens/comments after authorized
   transforms; a comment-only or remaining-null body is not empty. Body policies
   cover ordinary/control/case/function/lambda/record/namespace bodies, not enums,
   other lists or requirement bodies. Compactness does not relocate an opening
   brace governed by a separate header policy.
3. **Namespace indentation** uses rendered enclosing/content block anchors.
   `NoIndent` is zero relative indent; width zero resolves to `IndentWidth`, not
   zero indentation. Nested namespaces inherit the enclosing content base;
   `namespace A::B` has one body. Preserve retains the existing relationship;
   explicit modes do not alter ordinary function/control-body indentation.
4. **Expression packing** uses Fill/vertical owned operator sequences while
   operand groups retain independent syntax and decisions.
5. **Enum header/list independence** separates declaration-header brace
   placement from the shared enumerator list policy. Enumerator expressions are
   children; empty enums cannot invent item breaks. Body shape does not govern
   enums, and a broken enumerator list does not make its header broken.
6. **Owned vertical constraints and caps** lower to identified boundary gaps:
   namespace content edges, explicit access sections and adjacent complete
   definition items. Attach documentation/attributes to their declaration and
   trailing comments to the preceding item; never infer adjacency through a
   directive/protection barrier or move unattached comments to create a gap.

Vertical counts mean empty lines: at a broken boundary, `n` requires `n + 1`
newline sequences. Explicit counts are exact, subject to safety; a positive count
requires the applicable boundary to break, while zero only removes excess empty
lines and does not itself force or remove the boundary break. No phantom boundary
exists for empty namespace content or an empty access section. Member definition
separation replaces, rather than adds to, the general definition count.

These explicit vertical policies narrowly own their gaps even in surrounding
Preserve topology; they do not authorize neighboring reflow. `MaxEmptyLines` caps
editable existing vertical gaps, not protected bytes, token-interior newlines or
mandatory line-comment boundaries. Snapshot validation rejects an explicit or
resolved exact count above an explicit cap, even on inputs without such a gap or
under Preserve; report the conflicting options rather than silently clamping.

## 6. Protection and horizontal ownership

Disabled/sensitive/directive bytes and lexical/line/context boundaries outrank
layout, annotations and structural actions. Recovery or uncertain syntax becomes
local protected islands, retaining safe work elsewhere. There is no restore-after-
format strategy, legacy fallback or whole-file retry.

`GapContext = Preserved | Rebuilt` describes the owning property's regeneration,
not token provenance or whether text happened to change. Canonical edges and
separators can be Rebuilt between original tokens; inserted adjacencies are
Rebuilt subject to protection. A nested edit does not activate preserved outer
owners. `GeneratedSpace`/`GeneratedNoSpace` consult this context.

Horizontal `GapPolicy.Preserve` requires a further structural distinction:

- At **canonical topology-changing boundaries**, use the syntax-safe flat
  fallback **before fit**, even if that gap ultimately stays flat. Source tabs or
  spaces that a break would discard must not change a second invocation's fit.
- Preserved boundaries and genuinely flat-only, non-topology-changing adjacencies
  retain available raw horizontal bytes. Intentionally retained spelling may
  affect width; canonical topology does not depend on discarded spelling.

Do not infer topology ownership from the final break, generated-token identity or
before/after comparisons. This is an intended lowering/gap contract, not a claim
that all required extensions are already accepted code.

Line comments/directives may own CR before their following LF. A changed gap
requiring LF-first emits LF first, completing that token without inserting CR or
spaces before it; further generated newline sequences use the global newline
policy. Vertical gaps emit indentation only after the final newline, not on
intermediate blank lines. Optional borrowed source flat whitespace/indentation
preserves bytes without rewrites; explicitly regenerated indentation uses
anchors/spaces/tabs. These borrowed-gap and vertical-emission extensions also
remain target contracts rather than claimed completed integration.

## 7. Exactly three library annotations

Compile owning sorted metadata after final overrides; lowering, not the renderer,
resolves it against parsed syntax:

- **Chain spine:** traverse member receiver and call callee edges for `.`, `->`
  and repeated invocation such as `factory()(arg)(next)`. Empty root names mean
  AllSafe; nonempty names mean AnnotatedRoots, both gated by resolved ChainsLayout.
  Argument calls remain children, never links flattened into the outer spine.
- **First-string soft pin:** only a complete first string-literal expression,
  including adjacent literal concatenation, qualifies. A literal prefix of a
  larger expression does not. Global fit/hard boundaries/protection still win;
  `Always` outranks the preference if no other legal owner break can satisfy it.
- **Matched visual scopes:** match balanced pairs in the same parsed statement
  sequence, receiver and wrapper context. Preserve an existing extra level;
  do not invent visual nesting. Unmatched, ambiguous or preprocessor-crossing
  regions remain source-owned.

Annotations do not implicitly canonicalize preserved owners. The renderer sees
only generic groups/anchors, never names, regexes or width overrides. Macro syntax
hints and whitespace-sensitive protection metadata are separate semantics, not a
fourth layout annotation.

## 8. PMR ownership and bounded work

```text
configuration lifetime
  raw configuration --final overrides--> const scalar snapshot
                                      \-> owning sorted annotation snapshot
                                           (reused read-only across inputs)

one input-file lifetime
  owned source(s) --> tree(s) borrowing spelling
  file arena     --> analysis / plans / doc / decisions / composed maps
  scratch arena  --> temporary indices / explicit traversal stacks
                       | reset between phases; NO surviving views
  emission       --> final output / diagnostics / hunks / requested maps
                       | own all storage needed after file arena destruction
                       v
                    returned result
```

The tree may keep its existing internally owned arena; file-scoped document and
analysis storage reuse PMR infrastructure. Original and normalized source buffers
outlive every dependent tree/view through emission and semantic materialization.
Returned results either own their storage directly or own the backing resource;
they never retain scratch pointers or views into destroyed file/configuration
storage. No mutable global caches or per-token strings are needed.

Target work is bounded linear byte/token/node/document passes, with sorting for
protection intervals, ownership events, include groups and annotation metadata
where needed. Cache summaries once; use compact contiguous records, indexed
provenance and explicit stacks. Avoid unbounded recursion, repeated subtree or
suffix scans, peak scratch retained across files and combinatorial layout search.
This is a design constraint, not a measured speedup claim.

## 9. Atomic actions, maps and hunks

Brace insertion, empty-control conversion, null removal and include ordering are
explicit syntax-authorized actions. Validate complete operation ownership,
overlap/order, token boundaries and protected crossings before materialization.
Missing-brace pairs and include permutations cannot be partially applied. Parser
completeness alone is insufficient: a colon-ended controlled-label representation
can leave the following statement outside its apparent control range. Planning
must retain required statements and reject unsafe direct/nested label ownership,
using once-computed metadata rather than trusting a complete node blindly.

Include sorting authorizes permutation only of eligible **whole Directive-only
line atoms**, retaining their exact interiors/encoding. Disabled, sensitive,
explicit/hunk-excluded or other non-Directive reason unions forbid relocation;
continued/comment-bearing/conditional barriers cannot be crossed. This is not
permission to rewrite byte-protected directive text.

```text
original token IDs/source ranges
        -- atomic normalization + provenance --> normalized token ranges
        -- layout emission -------------------> rendered ranges
        -- authorized namespace edits --------> final result ranges
```

Keep original IDs for surviving/moved tokens, mark deletions absent, and identify
generated tokens by action/anchor. Project protection through copied provenance;
a generated adjacency cannot relax original immutable bytes. Compose maps, do
not assume token ordinals or old offsets survive transformations.

Namespace resolution verifies exact mapped spelling, boundary/interior/neighbor
ordering and comment attachment against original identities and final protection.
Same-spelling braces alone cannot validate a map: swapping a nested function's
close with a namespace close must be rejected locally. Authorized include
permutations remain distinguishable by provenance. Compute ordering summaries
once, not by rescanning every namespace subtree. Adding a comment must not swallow
following code/comments or introduce a newline; ShortNamespaceLines observes
strictly interior final-output lines, with zero disabling the add exemption.

Hunk eligibility constrains planning and lowering from the start: select permitted
layout units, protect exclusions and honor atomic spill rules. Do not format the
whole file and discard unauthorized edits afterward. Diagnostics/check/fix use
the same decisions and composed original positions, not separate layout models.

## 10. Validation, migration and rollout

Reviewable checkpoints are policy compilation, protection, the standalone group
core, syntax/annotation lowering, atomic transforms/maps, then full pipeline and
hunk integration. Each boundary needs independent acceptance; green component
tests do not imply that the replacement is integrated. Structural-action safety
and map validation remain gates, not assumptions inherited from parser status.

Use library-level goldens through shared test helpers for tokenization, parsing,
formatting and diagnostic behavior; keep full-file cases in existing suites.
Review every explicitly regenerated expectation (`UPDATE_GOLDEN=1`). Add properties
for idempotence, canonical safe-whitespace invariance, protected byte/token
identity, tree/source overload agreement, map composition, atomic hunk isolation,
actual shape and deep/adversarial inputs. Cover nested child breaks, singleton/
empty force-break cases, optional punctuation fit, CR/LF ownership, source tabs at
canonical boundaries, controlled labels and same-spelling stale brace maps.
No CLI-spawning tests are required or permitted by the repository test contract.

After integration, run `build/tests/tests` directly and the separate
`build/tests/allocation_tests`, not CTest. Before performance claims, measure paired
same-revision/configuration/build/environment runs on representative sizes and
all three profiles' respective external codebases: runtime, allocation count and
peak RSS. Keep external code out of committed fixtures; component checks alone
cannot establish corpus performance.

Migrate keys/help/templates/profiles and source-named tests together, documenting
deliberate output differences. Retire legacy orchestration, repair retries and
header snapshots at integration; no compatibility aliases or new fallback.
Acceptance requires reviewed end-to-end behavior, protections/maps/hunks and
bounded-resource evidence, not a stale test-count log or regenerated goldens alone.
