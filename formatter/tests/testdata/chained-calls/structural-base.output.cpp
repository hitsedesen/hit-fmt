void f() {
    engine()
        .Open()
        .Finish();
    auto result = engine()
        .Open()
        .Finish();
    auto nested = outer(
        inner(
        engine()
        .Open()
        .Finish()));
}
