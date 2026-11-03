void Do()
{
    Run(
        0,
        [&] {
            std::vector<int> values(
                first,
                second);
            std::vector<String> strings(first, second);
            Buffer splitValues
                (
                    first,
                    second
                );
        });
}

void Scoped()
{
    Run(
        0,
        [&] {
            if (Ready()) {
                encoder.OnBeginList();
                    std::vector<String> values(
                        first,
                        second,
                        third
                    );
                    Emit(values);
                encoder.OnEndList();
            }
        });
}

void UnclosedScope()
{
    Run(
        0,
        [&] {
            if (Ready()) {
                encoder.OnBeginList();
                    Emit(  value );
                    if (Nested()) {
                        std::vector<String> values(
                            first,
                            second);
                    }
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
            if (Ready()) {
                encoder.OnBeginList();
                    Emit(  value );
            }
        });
}

void Nested()
{
    Run(
        0,
        [&] {
            Run(
                1,
                [&] {
                    std::vector<std::pair<Key, Value>> values(first, /* unchanged spacing */ second);
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
        std::vector<String> values(
            first,
            second);
    });
    // hit-fmt on
}
