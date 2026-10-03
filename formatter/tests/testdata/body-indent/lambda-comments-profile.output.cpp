void RunTask()
{
    auto task = BIND(
        [&] {
            // Save the state before dispatch.
            Record(state);

            // The second operation uses the same snapshot.
            // Keep the explanation with its statement.
            Dispatch(state);

            // Detached explanation.

            Finish();
            /* A block comment is source-owned. */
            Reset();
        });
}
