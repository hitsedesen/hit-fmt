# Syntax-owned group fixtures

These synthesized full-file fixtures are registered in
`Formatter/FormatTest.Golden` as `group_syntax_nested` and
`group_syntax_messages`, using the shared `tests/golden.hpp` helpers.

`nested` covers nested parameter/capture/call/constructor lists, lambda and
control headers, ternary expressions and fluent calls. `messages` covers
complete adjacent-literal pins, a member call, nonliteral first arguments,
unannotated calls, internal literal-group breaks and unavoidable literal width.

Keep diagnostic and ownership properties in `formatter/tests/syntax_layout.cpp`
separate from the full-file goldens. Regenerate expectations only with explicit
`UPDATE_GOLDEN=1` and review the complete diff; normal runs must not update them.
Tests exercise the library, not the CLI.
