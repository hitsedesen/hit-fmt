# Formatter test fixtures

All source snippets in this directory are synthetic and were written for this
project. They exercise syntax and layout invariants modeled on real patterns
from other codebases without copying their code.

Both `.input.cpp` and `.output.cpp` fixtures are independent preservation
cases. Tests compare each file against itself on two parser/emitter passes
under multiple passive width dimensions; historical formatted outputs are
not regenerated or treated as a new formatting requirement.

`testdata/function_parameters/parameters.input.cpp` is shared by the
function-parameter layout cases. Each `.hit-fmt` file configures one policy
combination and has a matching `.output.cpp` golden file. Unlike the legacy
preservation cases, these compare formatted output and its second pass against
explicit expectations; they require the function-parameter rules to be
implemented.
