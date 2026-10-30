void f() {
    consume("short" "pair");
    consume("short" "pair");
    consume(
        "first fragment has some text "
        "second fragment has more text "
        "third fragment has the last text");
    REPORT("first fragment has some text "
        "second fragment has more text "
        "third fragment has the last text",
        value);
    const auto message =
        "first fragment has some text "
        "second fragment has more text "
        "third fragment has the last text";
    const auto shortMessage = "short" "pair";
    outer(
        other,
        "first fragment has some text "
        "second fragment has more text "
        "third fragment has the last text");
    consume(
        u8"first fragment has some text "
        u8"second fragment has more text "
        u8"third fragment has the last text");
    consume(
        R"tag(first raw fragment has some text)tag"
        R"tag(second raw fragment has more text)tag");
    consume(
        R"tag(first
raw line)tag"
        "tail");
    consume(
        "first fragment has some text " /* note */ "second fragment has more text "
        "third fragment has the last text");
    consume(
        "first fragment has some text " // note
        "second fragment has more text "
        "third fragment has the last text");
}
const char*GetText() {
    return "first fragment has some text "
        "second fragment has more text "
        "third fragment has the last text";
}

const char*GetShortText() {
    return "short" "pair";
}
