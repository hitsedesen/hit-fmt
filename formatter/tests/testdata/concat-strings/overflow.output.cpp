void f() {
    consume("foo "
        "bar");
    consume("abc "
        "def " "ghi "
        "jkl");
    consume("short"
        "ok");
    consume("foo "
        "bar", otherArgument);
    consume("https://"
        "example");
    consume(u8"escaped\\n"
        u8"suffix");
    consume("foo " /* keep */ "bar");
    consume("foo "
        "bar");
}
