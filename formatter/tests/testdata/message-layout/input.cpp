void Report() {
message("Value %v", value);
message("Values %v %v", first, second);
raise(code, "Invalid %v", value);
raise(ready, code, "Invalid %v %v", first, second);
event(logger, level, anchor, "Values %v %v", first, second);
receiver.message(code, "Values %v %v", first, second);
receiver->message(code, "Values %v %v", first, second);
message("Value " "%v", value);
message(R"(Value %v)", value);
message("Value %v", nested(first, second), "another literal", [] { return value; }());
message(check(first, second), "Value %v", value);
message("No format arguments");
raise(code, "No format arguments");
message(code, text, first, second);
message("prefix" + suffix, value);
message(select("nested"), text, value);
message_other(code, "Values %v %v", first, second);
message(/* context */ code, "Values %v %v", /* first */ first, second);
message(code, /* message */ "Values %v %v", first, /* second */ second);
message(code, "Values %v %v", first, // keep this comment
second);
// hit-fmt off
message(code, "Protected %v %v", first, second);
// hit-fmt on
}
