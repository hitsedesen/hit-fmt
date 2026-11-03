void Test() {
    constexpr std::string_view scm =
        "Branch: trunk\n"
        "Author: bar\n";
    const char* inlineFirst = "first"
        "second";
    if (ready) {
        constexpr auto encoded =
            u8"first"
            u8"second";
    }
    consume(
        "first"
        "second");
}
const char* global =
    "first"
    "second";
