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
    producer()
        .Transform<FirstType, SecondType>(
            alpha, beta)
        .Complete<ThirdType>(
            gamma, delta);
    result -= producer()
        .Transform<FirstType, SecondType>(
            alpha, beta)
        .Complete<ThirdType>(
            gamma, delta);
    engine()
        .Open<int>(
            alpha, beta)
        .Finish<int>(
            gamma, delta);
    outer(
        engine()
            .Open<Type>(
                alpha, beta)
            .Finish(
                gamma, delta));
}
