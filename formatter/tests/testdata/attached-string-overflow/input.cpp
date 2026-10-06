void Report() {
message("short", alpha, beta, gamma);
message("short");
message("123456789012345678");
message("1234567890123456789");
message("left" " right", alpha, beta, gamma);
message("first piece " "second piece " "third", value);
message("an indivisible literal which exceeds the global width", extra);
message("an indivisible literal which exceeds the global width" " tail", extra);
receiver.message("first piece " "second piece", extra);
message("first piece " "second piece");
message("an indivisible literal which exceeds the global width");
message(R"(raw first piece )" "second piece", extra);
message(u8"first piece " u8"second piece", extra);
outer(message("first piece " "second piece"), extra);
message(
    "first piece "
    "second piece", extra);
message("first piece " /* join */ "second piece", extra);
message("literal" + suffix, alpha, beta);
other("first piece " "second piece", extra);
message_with_suffix("first piece " "second piece", extra);
message(/* prefix */ "first piece " "second piece", extra);
message("first piece " "second piece" /* suffix */, extra);
message(R"(multi
line)" "tail", extra);
// clang-format off
message(  "first piece "    "second piece", extra);
// clang-format on
}
