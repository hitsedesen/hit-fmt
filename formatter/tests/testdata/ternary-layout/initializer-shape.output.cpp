void read() {
    auto point = byte < 0x80
        ? text::CodePoint(byte)
        : CurrentUnicodeCodePoint();
    auto shortPoint = flag ? first : second;
    auto nested = ready
        ? (flag ? firstValue : secondValue)
        : fallbackValue;
    auto longCondition =
        leftCondition && middleCondition && rightCondition
            ? firstValue
            : secondValue;
    auto longBranch = flag
        ? process(firstArgument, secondArgument, thirdArgument)
        : fallbackValue;
    auto call = transform(firstArgument, secondArgument, thirdArgument);
    target = flag ? firstValue : secondValue;
}
