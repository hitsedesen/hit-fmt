void inspect() {
    for (const auto& [label, action] : {
        types::entry{"opening-option", "retain-elements"},
        types::entry{"closing-option", "remove-elements"},
        types::entry{"nested-option", "keep-declarations"},
        types::entry{"final-option", "keep-statements"}}) {
        record(label, action);
    }
    for (auto value : {entry{"first-item", "first-policy"}, entry{"second-item", "second-policy"}, entry{"last-item", "last-policy"},}) {
        record(value);
    }
    auto entries = {types::entry{"source-name", "source-action"}, types::entry{"target-name", "target-action"} /*tail*/};
    record({entry{"initial-key", "initial-value"}, entry{"terminal-key", "terminal-value"},});
    int rows[][2] = {{firstLongElement, secondLongElement}, {thirdLongElement, fourthLongElement}};
    int existing[] = {firstLongElement, secondLongElement, thirdLongElement, fourthLongElement,};
    int compact[] = {1, 2,};
    record(actual, (types::collection{firstLongElement, secondLongElement, thirdLongElement}));
    record(actual, (types::collection<int>{firstLongElement, secondLongElement, thirdLongElement}));
    record(ReadCurrentCapacityForActiveEndpoint(), std::numeric_limits<ExternalType>::max());
    auto available = ReadCurrentCapacityForActiveEndpoint() != std::numeric_limits<ExternalType>::max();
    RAISE exception{} << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    WARN exception{code} << "a long initial message that will need wrapping when concatenated with "
        /* join */ "the continuation of the message";
    NOTICE types::exception{} + R"(a raw message with enough text to make concatenation wrap )"
        "and a tail";
    RAISE exception{} << "short" " text";
    const char* messages[] = {"a long initial message that will need wrapping when concatenated with "
        "the continuation of the message"};
}
