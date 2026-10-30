void f() {
    consume("header\n"
        "body\n"
        "footer");
    consume("left" "right");
    consume("one"
        "two"
        "three"
        "four");
    consume(u8"first"
        u8"second");
    consume(L"first"
        L"second");
    consume(R"tag(first)tag"
        R"tag(second)tag");
    consume(R"tag(first
line)tag"
        "tail");
    consume("first" /* note */
        "second");
    consume("first"
        /* note */ "second");
    consume("first" // note
        "second");
    const auto message =
        "first"
            "second";
}
