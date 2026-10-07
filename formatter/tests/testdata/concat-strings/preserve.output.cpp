void f() {
    consume("foo " "bar");
    consume("abc " "def " "ghi " "jkl");
    consume("short" "ok");
    consume("foo " "bar", otherArgument);
    consume("https://" "example");
    consume(u8"escaped\\n" u8"suffix");
    consume("foo " /* keep */ "bar");
    consume("foo "
        "bar");
    consume("hello"_world( 8 ));
    consume(R"tag(raw)tag"_world( 8 ));
    consume(u8"hello"_world( 8 ));
    consume(U'x'_character( 8 ));
    consume("head" /* keep */ "tail"_world( 8 ));
    consume("head"_world "tail"( 8 ));
}
