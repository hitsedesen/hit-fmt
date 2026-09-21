# Formatter test fixtures

All source snippets in this directory are synthetic and were written for this
project. They exercise syntax and layout invariants modeled on real patterns
from other codebases without copying their code.

Formatting behavior belongs in paired `.input.cpp` and `.output.cpp` golden
files. Unit tests are reserved for API configuration and parser integration
that cannot be represented by a default-style golden fixture.
