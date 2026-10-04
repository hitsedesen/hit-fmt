void inspect()
{
    for (
        const auto& [label, action] :
        {
            types::entry{"opening-option", "retain-elements"},
        types::entry{"closing-option", "remove-elements"},
        types::entry{"nested-option", "keep-declarations"},
        types::entry{"final-option", "keep-statements"}
        })
    {
        record(label, action);
    }
    for (
        auto value :
        {
            entry{"first-item", "first-policy"}, entry{"second-item", "second-policy"}, entry{"last-item", "last-policy"},
        })
    {
        record(value);
    }
    auto entries =
        {
            types::entry{"source-name", "source-action"}, types::entry{"target-name", "target-action"} /*tail*/};
    record(
        {
            entry{"initial-key", "initial-value"}, entry{"terminal-key", "terminal-value"},
        });
    int rows[][2] = {
        {firstLongElement, secondLongElement},
        {thirdLongElement, fourthLongElement}
    };
    int existing[] = {
        firstLongElement,
        secondLongElement,
        thirdLongElement,
        fourthLongElement
    };
    int compact[] = {1, 2};
}
