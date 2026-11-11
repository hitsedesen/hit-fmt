void Grouping(bool ready) {
    Prepare();
    auto first = Read();
    auto second = Read();
    if (ready) {
        Use(first, second);
    }
    int count{3};
    for (int i = 0; i < count; ++i) {
        Step(i);
    }
    int limit(4);
    while (ready) {
        Use(limit);
    }
    auto [value, pending] = Next();
    // Keep this check attached.
    while (pending) {
        Use(value);
    }
    int left = 1, right = 2;
    [[likely]] if (ready) {
        Use(left, right);
    }
    auto callback = [] {
        return true;
    };
    if (callback()) {
        Run();
    }
    auto spaced = Read();

    if (ready) {
        Use(spaced);
    }
    int uninitialized;

    if (ready) {
        uninitialized = 1;
    }
    using Value = int;

    if (ready) {
        Run();
    }
    void Function();

    if (ready) {
        Function();
    }
    Assign();

    if (ready) {
        Run();
    }
    auto state = Read();
    state = Read();

    while (ready) {
        Use(state);
    }
    int (*factory)() = nullptr;
    if (ready) {
        Use(factory);
    }
    int declarationWithDefault(int argument = 3);

    if (ready) {
        Run();
    }
    auto beforeDirective = Read();
#if ENABLED
    if (ready) {
        Use(beforeDirective);
    }
#endif
}
