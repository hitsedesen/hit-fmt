void choose() {
    value =
        condition
        ? trueFirst +
        trueSecond
        : falseFirst +
        falseSecond;

    value =
        guard
        ? width * factor +
        (IsDecimal(digit)
        ? Convert(digit) - zero
        : Normalize(digit) - base +
        bias)
        : fallback;

    auto nested = condition ?
        outer(inner(
        argument)) :
        other;

    auto ordinary = outer(inner(
        argument));

    auto compact = condition ? left + right : up + down;
    auto inlineBranch = condition ? outer(inner(
        argument)) : fallback;

    auto result = LeftCondition() &&
        RightCondition()
        ? left +
        right
        : otherLeft +
        otherRight;
}
