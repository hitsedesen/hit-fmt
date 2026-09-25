# Formatter test fixtures

All source snippets in this directory are synthetic and were written for this
project. They exercise syntax and layout invariants modeled on real patterns
from other codebases without copying their code.

Both `.input.cpp` and `.output.cpp` fixtures are independent preservation
cases. Tests compare each file against itself on two parser/emitter passes
under multiple width dimensions with the default `Preserve` policy;
historical formatted outputs are not regenerated or treated as a new
formatting requirement.

`testdata/function_parameters/input.cpp` is shared by the
function-parameter layout cases. Each `.hit-fmt` file configures one policy
combination and has a matching `.output.cpp` golden file. Unlike the legacy
preservation cases, these compare formatted output and its second pass against
explicit expectations for function declarations and definitions while
leaving calls unchanged.

`testdata/namespace_comments/input.cpp` is shared by the namespace-comment
cases. The configured `.hit-fmt` files and matching `.output.cpp` files
cover preservation, removal, correction, missing comments, nested and
anonymous namespaces, and interaction with brace placement. Each output
is also checked on a second formatting pass.
