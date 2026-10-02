# Replacement lowering fixtures (partial component checkpoint)

These synthesized full-file fixtures are for eventual registration in the
existing `Formatter/FormatTest.Golden` suite, not a new permanent full-file test
suite. `nested` covers nested parameter/capture/call/constructor lists, lambda
and control headers, ternary expressions and fluent calls. `messages` covers
complete adjacent-literal pins, a member call, nonliteral first arguments,
unannotated calls, internal literal-group breaks and unavoidable literal width.

After replacement integration, register `group_syntax_nested` and
`group_syntax_messages` using the existing formatter fixture convention:
`<name>.input.cpp`, `<name>.hit-fmt`, `<name>.output.cpp`. Keep property tests in
`formatter/tests/syntax_layout.cpp` separate. Do not call the CLI in tests.

The current isolated harness is `.validation/fixtures.cpp`; it uses the shared
`tests/golden.hpp` helpers and underlying lowering APIs. Expectations were
regenerated only with explicit `UPDATE_GOLDEN=1` and reviewed in full. A normal
run never updates them. Do not blindly replace expectations when integrating:
the ownership follow-up fixes header/wrapper/control-arm projection and has
revalidated the complete focused suite against reviewed function-try spacing.
Independent lowering review remains required before component acceptance.
