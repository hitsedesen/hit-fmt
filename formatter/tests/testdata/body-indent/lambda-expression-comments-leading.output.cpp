void f() {
    Run(
        1,
        config,
        [] {
            const auto n = 1 + /* comment */
                Value();
            Use(n);
            const auto m = 2 + // operand follows
                /* another comment */
                Value();
            Use(m);
            const auto assigned = /* initializer follows */
                Value();
            Use(assigned);
            if (
                n && /* condition follows */
                Ready()
            ) {
                Use(n);
            }
            Run(
                2,
                config,
                [] {
                    return 3 + /* nested body */
                        Value();
                }
            );
            Use(
                /* call argument */
                n
            );
            // A statement comment stays at the body indentation.
            Finish();
        }
    );
    const auto outside = 4 + /* ordinary body */
        Value();
    Use(outside);
}
