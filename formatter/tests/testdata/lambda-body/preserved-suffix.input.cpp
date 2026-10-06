void Do()
{
    Run(0, [&] {
        std::vector<int> values(
            first,
            second);
        Buffer splitValues
            (
                first,
                second
            );
    });
}

void Scoped()
{
    Run(0, [&] {
        if (Ready()) {
            encoder.OnBeginList();
                Buffer values(
                    first,
                    second,
                    third
                );
                Emit(values);
            encoder.OnEndList();
        }
    });
}

void MoveLeft()
{
    Run(
        0,
            [&] {
                Buffer values(
                    first,
                    second);
            });
}

void Nested()
{
    Run(0, [&] {
        Run(1, [&] {
            Buffer values(
                first,  /* unchanged spacing */ second);
            auto literal = R"(literal
    contents)";
        });
    });
}

void Unmoved()
{
    Buffer values(
        first,  second);
    Buffer inlineValues(first,  second);
    // hit-fmt off
    Run(0, [&] {
        Buffer values(
            first,
            second);
    });
    // hit-fmt on
}
