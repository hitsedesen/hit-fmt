void inspect() {
    Foo(Bar(Baz(
        "Hello world")));

    Foo(
        Bar(
            Baz(
                "hello world")));

    if (Foo(
        "bar"))
    {
        Print("hello");
    }

    if (
        Foo(
            "bar"))
    {
        Print("hello");
    }

    auto result = WaitFor(Post(
        request));

    auto continued =
        WaitFor(Post(
            request));

    bool ready =
        Left() ||
        Right();
}
