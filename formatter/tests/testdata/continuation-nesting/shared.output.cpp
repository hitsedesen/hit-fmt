void inspect() {
    bool joined =
        first() ||
            (second() &&
                third());
    auto result =
        outer(
            inner(
                alpha, beta), gamma);
    int values[][2] =
        {
            {
                alpha, beta
            }, {
                gamma, delta
            }
        };
    if (tooLarge) {
        throw LimitError(
            renderMessage(
                "Size {} exceeds {}", value, limit));
    }
    if (!allMatch(
        items, [&](auto item) {
            bool valid =
                first() ||
                    second();
            return accept(
                item, valid);
        }))
    {
        bool ready =
            first() ||
                (second() &&
                    third());
        auto bodyResult =
            outer(
                inner(
                    alpha, beta), gamma);
        if (ready &&
            joined)
        {
            auto nestedResult =
                outer(
                    inner(
                        alpha, beta), gamma);
        }
    }
    for (int index =
        initial() +
            adjustment(); index <
                bound(); ++index)
    {
        bool remaining =
            first() ||
                second();
    }
}
